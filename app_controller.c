#include "app_controller.h"

#include "cat.h"
#include "config.h"
#include "db.h"
#include "cty.h"
#include "dxcluster.h"
#include "export.h"
#include "globals.h"
#include "qso.h"
#include "suggestion.h"
#include "stats.h"

#include <stdlib.h>

static char input_buffer[256];
static int input_len = 0;

enum {
  ENTRY_FIELD_CALL = 0,
  ENTRY_FIELD_RST = 1,
  ENTRY_FIELD_COMMENTS = 2,
  ENTRY_FIELD_COUNT = 3
};

static char entry_call[32];
static char entry_rst[8];
static char entry_comments[128];
static int active_entry_field = ENTRY_FIELD_CALL;

static char status_text[128] = "Ready";
static char dxcc_text[128] = "";
static char info_text[128] = "";
static char display_info[128] = "";

int app_debug_enabled = 0;

int last_cq = 0;
int last_itu = 0;
int cty_update_in_progress = 0;
int call_suggestion_available = 0;
int call_suggestion_count = 0;
int call_suggestion_selected_index = 0;
char call_suggestion_matches[CALL_SUGGESTION_MAX][CALL_SUGGESTION_LEN] = {{0}};

#define MAX_CALL_HISTORY 20000

static char call_history[MAX_CALL_HISTORY][32];
static int call_history_count = 0;
static CallSuggestionList call_suggestions;

static bool cluster_view = true;
static int cluster_scroll = 0;
static bool export_prompt_mode = false;
static int manual_entry_freq_khz = 14074;

#define NAMED_LOG_LIST_MAX 12

static void sync_callsign_suggestion_state(void);

static void update_composed_input_line(void);

/*
 * Reset split QSO entry fields and move focus to CALL.
 *
 * @return Nothing.
 */
static void clear_entry_fields(void) {
  entry_call[0] = 0;
  entry_rst[0] = 0;
  entry_comments[0] = 0;
  active_entry_field = ENTRY_FIELD_CALL;
  update_composed_input_line();
}

/*
 * Build a display-only composed input line from split entry fields.
 *
 * @return Nothing.
 */
static void update_composed_input_line(void) {
  if (entry_call[0] || entry_rst[0] || entry_comments[0]) {
    snprintf(input_buffer, sizeof(input_buffer), "%s %s %s", entry_call,
             entry_rst, entry_comments);
  } else {
    input_buffer[0] = 0;
  }

  input_len = (int)strlen(input_buffer);
}

/*
 * Return writable pointer and size for the currently active entry field.
 *
 * @param out_size Destination for selected field capacity.
 * @return Pointer to active field buffer.
 */
static char *active_field_buffer(size_t *out_size) {
  if (out_size)
    *out_size = 0;

  switch (active_entry_field) {
  case ENTRY_FIELD_CALL:
    if (out_size)
      *out_size = sizeof(entry_call);
    return entry_call;
  case ENTRY_FIELD_RST:
    if (out_size)
      *out_size = sizeof(entry_rst);
    return entry_rst;
  case ENTRY_FIELD_COMMENTS:
  default:
    if (out_size)
      *out_size = sizeof(entry_comments);
    return entry_comments;
  }
}

/*
 * Move editing focus to the next split entry field.
 *
 * @return Nothing.
 */
static void advance_entry_field(void) {
  active_entry_field = (active_entry_field + 1) % ENTRY_FIELD_COUNT;
}

/*
 * Append one printable character to the active split entry field.
 *
 * @param key Printable key code.
 * @return 1 on success, or 0 if the field is full.
 */
static int append_to_active_field(int key) {
  size_t size = 0;
  char *field = active_field_buffer(&size);

  if (!field || size < 2)
    return 0;

  size_t len = strlen(field);
  if (len >= size - 1)
    return 0;

  field[len] = (char)key;
  field[len + 1] = 0;
  return 1;
}

/*
 * Delete one character from the active split entry field.
 *
 * @return 1 if a character was removed, otherwise 0.
 */
static int backspace_active_field(void) {
  char *field = active_field_buffer(NULL);
  if (!field)
    return 0;

  size_t len = strlen(field);
  if (len == 0)
    return 0;

  field[len - 1] = 0;
  return 1;
}

/*
 * Build one command line from split fields.
 *
 * @param out Destination buffer.
 * @param out_size Destination size in bytes.
 * @return Nothing.
 */
static void compose_command_line(char *out, size_t out_size) {
  if (!out || out_size < 2)
    return;

  out[0] = 0;

  if (!entry_call[0])
    return;

  snprintf(out, out_size, "%s", entry_call);

  if (entry_rst[0]) {
    strncat(out, " ", out_size - strlen(out) - 1);
    strncat(out, entry_rst, out_size - strlen(out) - 1);
  }

  if (entry_comments[0]) {
    strncat(out, " ", out_size - strlen(out) - 1);
    strncat(out, entry_comments, out_size - strlen(out) - 1);
  }
}

/*
 * Extract the first callsign-like token from an input string.
 *
 * @param input Source text to scan.
 * @param out Destination buffer for the token.
 * @param out_size Size of the destination buffer.
 * @return 1 if a token was extracted, otherwise 0.
 */
static int extract_callsign_token(const char *input, char *out, size_t out_size) {
  if (!input || !out || out_size < 2)
    return 0;

  out[0] = 0;

  size_t len = strlen(input);
  size_t start = 0;

  while (start < len && isspace((unsigned char)input[start]))
    start++;

  if (start >= len)
    return 0;

  size_t end = start;
  while (end < len && !isspace((unsigned char)input[end]) && input[end] != ';')
    end++;

  size_t token_len = end - start;
  if (token_len < 2 || token_len >= out_size)
    return 0;

  for (size_t i = 0; i < token_len; i++)
    out[i] = (char)toupper((unsigned char)input[start + i]);

  out[token_len] = 0;

  return 1;
}

/*
 * Keep the in-memory call history buffer up to date.
 *
 * @param call Callsign to append.
 * @return Nothing.
 */
static void call_history_add_memory(const char *call) {
  if (!call || !call[0])
    return;

  if (call_history_count < MAX_CALL_HISTORY) {
    snprintf(call_history[call_history_count], sizeof(call_history[0]), "%s", call);
    call_history_count++;
    return;
  }

  memmove(call_history, call_history + 1,
          sizeof(call_history[0]) * (MAX_CALL_HISTORY - 1));
  snprintf(call_history[MAX_CALL_HISTORY - 1], sizeof(call_history[0]), "%s",
           call);
}

/*
 * Append a callsign to the persistent call-history store.
 *
 * @param call Callsign to append.
 * @return Nothing.
 */
static void call_history_append_file(const char *call) {
  db_append_call_history(call);
}

/*
 * Record a completed input line as call history when it contains a callsign.
 *
 * @param input Raw input line.
 * @return Nothing.
 */
static void call_history_record_from_input(const char *input) {
  char call[32];

  if (!extract_callsign_token(input, call, sizeof(call)))
    return;

  if (strcmp(call, "EXPORT") == 0 || strcmp(call, "INVALID") == 0 ||
      strcmp(call, "QUIT") == 0 || strcmp(call, "NEWLOG") == 0 ||
      strcmp(call, "CLEAR") == 0 || strcmp(call, "PREVLOG") == 0 ||
      strcmp(call, "OPENPREV") == 0 || strcmp(call, "PREVIOUS") == 0 ||
      strcmp(call, "OPENLOG") == 0 || strcmp(call, "LOGS") == 0)
    return;

  call_history_add_memory(call);
  call_history_append_file(call);
}

/*
 * Reload call history from the database-backed store.
 *
 * @param path Unused compatibility parameter.
 * @return Nothing.
 */
static void call_history_load_file(const char *path) {
  (void)path;

  int count = 0;
  db_load_call_history(call_history, MAX_CALL_HISTORY, &count);
  call_history_count = count;
}

/*
 * Clear the active suggestion list and cached suggestion state.
 *
 * @return Nothing.
 */
static void clear_callsign_suggestion(void) {
  call_suggestion_list_clear(&call_suggestions);
  call_suggestion_available = 0;
  call_suggestion_count = 0;
  call_suggestion_selected_index = 0;

  for (int i = 0; i < CALL_SUGGESTION_MAX; i++)
    call_suggestion_matches[i][0] = 0;
}

/*
 * Reset all state associated with the currently loaded logbook.
 *
 * @return Nothing.
 */
static void reset_loaded_log_state(void) {
  qso_init();
  call_history_load_file("call_history.txt");
  clear_callsign_suggestion();
  clear_entry_fields();
  export_prompt_mode = false;
  dxcc_text[0] = 0;
  info_text[0] = 0;
  display_info[0] = 0;
  last_cq = 0;
  last_itu = 0;

  stats_update();
}

/*
 * Check whether a string contains only decimal digits.
 *
 * @param s Input string.
 * @return 1 if the string is non-empty and numeric, otherwise 0.
 */
static int is_digits_only(const char *s) {
  if (!s || !s[0])
    return 0;

  for (const char *p = s; *p; p++) {
    if (!isdigit((unsigned char)*p))
      return 0;
  }

  return 1;
}

/*
 * Validate frequency range used in QSO input.
 *
 * @param freq_khz Frequency in kHz.
 * @return 1 if valid, otherwise 0.
 */
static int is_valid_frequency_khz(int freq_khz) {
  if (freq_khz < 1000)
    return 0;

  if (freq_khz > 500000)
    return 0;

  return 1;
}

/*
 * Resolve QSO frequency, preferring live CAT over manual fallback.
 *
 * @return Frequency in kHz.
 */
static int resolve_qso_frequency_khz(void) {
  int cat_freq_khz = 0;

  if (cat_is_connected() && cat_get_frequency_khz(&cat_freq_khz) == 0 &&
      is_valid_frequency_khz(cat_freq_khz)) {
    return cat_freq_khz;
  }

  return manual_entry_freq_khz;
}

int app_controller_get_active_frequency_khz(void) {
  return resolve_qso_frequency_khz();
}

/*
 * Trim leading and trailing whitespace from a string in place.
 *
 * @param s String to trim.
 * @return Nothing.
 */
static void trim_whitespace_in_place(char *s) {
  if (!s)
    return;

  size_t len = strlen(s);
  while (len > 0 && isspace((unsigned char)s[len - 1])) {
    s[len - 1] = 0;
    len--;
  }

  size_t start = 0;
  while (s[start] && isspace((unsigned char)s[start]))
    start++;

  if (start > 0)
    memmove(s, s + start, strlen(s + start) + 1);
}

/*
 * Archive the current logbook, clear it, and reset UI state.
 *
 * @return Nothing.
 */
static void create_new_clean_log(void) {
  if (db_archive_current_logbook() != 0 || db_clear_logbook() != 0) {
    snprintf(status_text, sizeof(status_text), "New log failed");
    return;
  }

  reset_loaded_log_state();
  snprintf(status_text, sizeof(status_text), "New clean log created");
}

/*
 * Archive the current logbook under a name and reset UI state.
 *
 * @param name Optional logbook name.
 * @return Nothing.
 */
static void create_new_named_log(const char *name) {
  if (!name || !name[0]) {
    create_new_clean_log();
    return;
  }

  if (db_archive_current_logbook_named(name) != 0) {
    snprintf(status_text, sizeof(status_text), "New named log failed");
    return;
  }

  reset_loaded_log_state();
  snprintf(status_text, sizeof(status_text), "New log created: %s", name);
}

/*
 * Reopen the previously archived logbook.
 *
 * @return Nothing.
 */
static void open_previous_log(void) {
  if (db_open_previous_logbook() != 0) {
    snprintf(status_text, sizeof(status_text), "No previous log available");
    return;
  }

  reset_loaded_log_state();
  snprintf(status_text, sizeof(status_text), "Previous log opened");
}

/*
 * Format the list of named logbooks into the status area.
 *
 * @return Nothing.
 */
static void list_named_logs(void) {
  DBNamedLogbook items[NAMED_LOG_LIST_MAX];
  int count = 0;

  if (db_list_named_logbooks(items, NAMED_LOG_LIST_MAX, &count) != 0) {
    snprintf(status_text, sizeof(status_text), "Cannot read named logs");
    return;
  }

  if (count <= 0) {
    info_text[0] = 0;
    snprintf(status_text, sizeof(status_text), "No named logs in DB");
    return;
  }

  char line[sizeof(info_text)] = {0};
  size_t used = 0;

  for (int i = 0; i < count; i++) {
    char piece[96];
    snprintf(piece, sizeof(piece), "%lld:%s(%d)%s", items[i].id, items[i].name,
             items[i].qso_count, (i + 1 < count) ? " | " : "");

    size_t piece_len = strlen(piece);
    if (used + piece_len >= sizeof(line) - 1)
      break;

    memcpy(line + used, piece, piece_len);
    used += piece_len;
    line[used] = 0;
  }

  snprintf(info_text, sizeof(info_text), "%s", line);
  snprintf(status_text, sizeof(status_text),
           "Named logs: %d (use openlog <id|name>)", count);
}

/*
 * Open a named logbook by id or name selector.
 *
 * @param selector Logbook id or name.
 * @return Nothing.
 */
static void open_named_log_selector(const char *selector) {
  if (!selector || !selector[0]) {
    snprintf(status_text, sizeof(status_text), "Usage: openlog <id|name>");
    return;
  }

  int rc = -1;
  if (is_digits_only(selector)) {
    rc = db_open_named_logbook_by_id(atoll(selector));
  } else {
    rc = db_open_named_logbook_by_name(selector);
  }

  if (rc != 0) {
    snprintf(status_text, sizeof(status_text), "Named log not found: %s",
             selector);
    return;
  }

  reset_loaded_log_state();
  snprintf(status_text, sizeof(status_text), "Log opened: %s", selector);
}

/*
 * Refresh call suggestions for the current input buffer.
 *
 * @param input Input buffer to analyze.
 * @return Nothing.
 */
static void refresh_callsign_suggestion(const char *input) {
  clear_callsign_suggestion();

  call_suggestion_refresh(&call_suggestions, input, call_history,
                          call_history_count);
  sync_callsign_suggestion_state();
}

/*
 * Apply the currently selected callsign suggestion to the input buffer.
 *
 * @param input Input buffer to modify.
 * @param len Current input length, updated on success.
 * @return 1 if a suggestion was applied, otherwise 0.
 */
static int apply_callsign_suggestion(char *input, int *len) {
  return call_suggestion_apply(&call_suggestions, input, len,
                               sizeof(input_buffer));
}

/*
 * Synchronize public suggestion state from the internal suggestion list.
 *
 * @return Nothing.
 */
static void sync_callsign_suggestion_state(void) {
  call_suggestion_count = call_suggestions.count;
  call_suggestion_selected_index = call_suggestions.selected;
  call_suggestion_available = call_suggestion_count > 0;

  for (int i = 0; i < CALL_SUGGESTION_MAX; i++) {
    if (i < call_suggestion_count)
      snprintf(call_suggestion_matches[i], CALL_SUGGESTION_LEN, "%s",
               call_suggestions.matches[i]);
    else
      call_suggestion_matches[i][0] = 0;
  }
}

/*
 * Export the current logbook to ADIF using an explicit filename.
 *
 * @param adif_file Destination ADIF filename.
 * @return 0 on success, or -1 on failure.
 */
static int export_with_adif_filename(const char *adif_file) {
  const char *default_csv = "log.csv";

  if (!adif_file || !adif_file[0])
    return -1;

  if (export_csv(default_csv) != 0)
    return -1;

  if (export_adif(adif_file) != 0)
    return -1;

  snprintf(status_text, sizeof(status_text), "Exported ADIF: %s", adif_file);

  return 0;
}

/*
 * Parse an export command and choose the ADIF destination filename.
 *
 * @param cmd Raw command line.
 * @return 0 on success, or -1 on failure.
 */
static int export_with_optional_adif(const char *cmd) {
  const char *default_adif = "log.adi";
  const char *adif_file = default_adif;

  if (cmd) {
    const char *p = cmd;

    while (*p && isspace((unsigned char)*p))
      p++;

    if (strncmp(p, "export", 6) != 0)
      return -1;

    p += 6;

    while (*p && isspace((unsigned char)*p))
      p++;

    if (*p)
      adif_file = p;
  }

  return export_with_adif_filename(adif_file);
}

/*
 * Handle a typed command line entered into the controller.
 *
 * @param cmd Raw command text.
 * @return 1 if the input was treated as a command, otherwise 0.
 */
static int process_command(const char *cmd) {
  char command[32] = {0};
  char arg[192] = {0};

  if (!cmd || !cmd[0])
    return 0;

  const char *p = cmd;
  while (*p && isspace((unsigned char)*p))
    p++;

  int i = 0;
  while (*p && !isspace((unsigned char)*p) && i < (int)sizeof(command) - 1) {
    command[i++] = (char)tolower((unsigned char)*p);
    p++;
  }
  command[i] = 0;

  while (*p && isspace((unsigned char)*p))
    p++;

  snprintf(arg, sizeof(arg), "%s", p);
  trim_whitespace_in_place(arg);

  if (strncmp(cmd, "export", 6) == 0) {
    if (export_with_optional_adif(cmd) != 0)
      snprintf(status_text, sizeof(status_text), "Export failed");

    return 1;
  }

  if (strcmp(command, "newlog") == 0 || strcmp(command, "clear") == 0) {
    if (arg[0])
      create_new_named_log(arg);
    else
      create_new_clean_log();

    return 1;
  }

  if (strcmp(command, "prevlog") == 0 || strcmp(command, "openprev") == 0 ||
      strcmp(command, "previous") == 0) {
    open_previous_log();
    return 1;
  }

  if (strcmp(command, "logs") == 0) {
    list_named_logs();
    return 1;
  }

  if (strcmp(command, "openlog") == 0) {
    open_named_log_selector(arg);
    return 1;
  }

  if (strcmp(command, "invalid") == 0) {
    if (qso_count > 0) {
      qso_mark_invalid(qso_count - 1);

      snprintf(status_text, sizeof(status_text), "Last QSO toggled INVALID");
    }

    return 1;
  }

  return 0;
}

/*
 * Update the DXCC display state based on the current input text.
 *
 * @param input Current input buffer.
 * @return Nothing.
 */
static void update_dxcc_from_input(const char *input) {
  dxcc_text[0] = 0;
  last_cq = 0;
  last_itu = 0;

  if (!input || !input[0])
    return;

  char call[32] = {0};
  size_t len = 0;
  const char *p = input;

  while (*p && isspace((unsigned char)*p))
    p++;

  while (*p && *p != ';' && !isspace((unsigned char)*p) &&
         len < sizeof(call) - 1) {
    char c = (char)toupper((unsigned char)*p);
    if (c == '/')
      break;

    call[len++] = c;
    p++;
  }

  call[len] = 0;

  if (len < 2)
    return;

  const CtyEntry *cty = cty_lookup(call);

  if (cty) {
    snprintf(dxcc_text, sizeof(dxcc_text), "%s", cty->country);

    last_cq = cty->cq_zone;
    last_itu = cty->itu_zone;
  } else {
    snprintf(dxcc_text, sizeof(dxcc_text), "Unknown");
  }
}

/*
 * Finalize a parsed QSO line and refresh derived UI state.
 *
 * @param line QSO input line.
 * @return Nothing.
 */
static void process_qso(const char *line) {
  int idx = qso_add(line, status_text, sizeof(status_text));

  if (idx < 0)
    return;

  QSO *q = &logbook[idx];

  snprintf(info_text, sizeof(info_text), "%s %s", q->band, q->mode);

  if (q->country[0] && strcmp(q->country, "UNKNOWN") != 0) {
    snprintf(dxcc_text, sizeof(dxcc_text), "%s", q->country);

    last_cq = q->cq_zone;
    last_itu = q->itu_zone;
  } else {
    snprintf(dxcc_text, sizeof(dxcc_text), "Unknown");
    last_cq = 0;
    last_itu = 0;
  }

  stats_update();
}

/*
 * Refresh the rendered info line based on suggestions and current input.
 *
 * @return Nothing.
 */
static void update_display_info(void) {
  if (call_suggestion_available && !export_prompt_mode) {
    const char *selected = call_suggestion_selected(&call_suggestions);
    if (selected && call_suggestion_count > 1) {
      snprintf(display_info, sizeof(display_info), "Suggest: %s (+%d more) [Tab]",
               selected, call_suggestion_count - 1);
    } else if (selected) {
      snprintf(display_info, sizeof(display_info), "Suggest: %s [Tab]", selected);
    } else {
      snprintf(display_info, sizeof(display_info), "%s", info_text);
    }
  } else {
    snprintf(display_info, sizeof(display_info), "%s", info_text);
  }
}

/*
 * Initialize shared application state and background services.
 *
 * @return 0 on success.
 */
int app_controller_init(void) {
  memset(input_buffer, 0, sizeof(input_buffer));
  input_len = 0;
  clear_entry_fields();
  memset(call_history, 0, sizeof(call_history));
  call_history_count = 0;
  snprintf(status_text, sizeof(status_text), "Ready");
  dxcc_text[0] = 0;
  info_text[0] = 0;
  display_info[0] = 0;
  cluster_view = true;
  cluster_scroll = 0;
  export_prompt_mode = false;
  manual_entry_freq_khz = 14074;
  cty_update_in_progress = 0;
  last_cq = 0;
  last_itu = 0;
  clear_callsign_suggestion();

  if (config_load("logger.conf") != 0)
    fprintf(stderr, "Cannot load logger.conf\n");

  cty_load("wl_cty.dat");
  qso_init();
  call_history_load_file("call_history.txt");

  if (app_debug_enabled) {
    fprintf(stderr,
            "[debug] app_controller_init: cluster_view=%d, starting DXCluster\n",
            cluster_view ? 1 : 0);
  }

  if (dxcluster_start() != 0) {
    snprintf(status_text, sizeof(status_text), "DXCluster start failed");
    update_display_info();
    return -1;
  }
  update_display_info();

  return 0;
}

/*
 * Shut down shared application state and stop background services.
 *
 * @return Nothing.
 */
void app_controller_shutdown(void) {
  dxcluster_stop();
  db_shutdown();
}

/*
 * Copy the current render state into out for UI consumers.
 *
 * @param out Destination structure to fill.
 * @return Nothing.
 */
void app_controller_get_render_state(AppRenderState *out) {
  if (!out)
    return;

  update_display_info();

  out->input = input_buffer;
  out->input_call = entry_call;
  out->input_rst = entry_rst;
  out->input_comments = entry_comments;
  out->active_input_field = active_entry_field;
  out->status = status_text;
  out->dxcc = dxcc_text;
  out->info = display_info;
  out->cluster_view = cluster_view;
  out->cluster_scroll = cluster_scroll;
}

/*
 * Download and reload the latest CTY database.
 *
 * @return Nothing.
 */
void app_controller_perform_cty_update(void) {
  if (cty_download_latest("wl_cty.dat") == 0) {
    int loaded = cty_load("wl_cty.dat");
    if (loaded >= 0)
      snprintf(status_text, sizeof(status_text), "CTY updated (%d entries)",
               loaded);
    else
      snprintf(status_text, sizeof(status_text), "Downloaded CTY, reload failed");
  } else {
    snprintf(status_text, sizeof(status_text), "CTY download failed");
  }

  cty_update_in_progress = 0;
}

/*
 * Handle a translated key code and update shared controller state.
 *
 * @param key One of the APP_KEY_* values.
 * @return The controller event to propagate to the UI.
 */
AppControllerEvent app_controller_handle_key(int key) {
  if (key == APP_KEY_NONE || key == APP_KEY_RESIZE)
    return APP_CTRL_EVENT_NONE;

  if (key == APP_KEY_F10) {
    return APP_CTRL_EVENT_EXIT;
  }

  if (cty_update_in_progress)
    return APP_CTRL_EVENT_NONE;

  if (key == APP_KEY_F2) {
    create_new_clean_log();
    return APP_CTRL_EVENT_NONE;
  }

  if (key == APP_KEY_F3) {
    open_previous_log();
    return APP_CTRL_EVENT_NONE;
  }

  if (key == APP_KEY_F4) {
    export_prompt_mode = true;
    clear_callsign_suggestion();
    input_buffer[0] = 0;
    input_len = 0;
    snprintf(status_text, sizeof(status_text),
             "Enter ADIF filename and press Enter (Esc to cancel)");
    return APP_CTRL_EVENT_NONE;
  }

  if (export_prompt_mode && key == APP_KEY_ESC) {
    export_prompt_mode = false;
    input_buffer[0] = 0;
    input_len = 0;
    snprintf(status_text, sizeof(status_text), "Export cancelled");
    return APP_CTRL_EVENT_NONE;
  }

  if (!export_prompt_mode && key == APP_KEY_ESC) {
    clear_entry_fields();
    clear_callsign_suggestion();
    dxcc_text[0] = 0;
    return APP_CTRL_EVENT_NONE;
  }

  if (key == APP_KEY_F6) {
    stats_update();
    snprintf(status_text, sizeof(status_text), "STATS updated");
  }

  if (key == APP_KEY_F5) {
    cluster_view = !cluster_view;
    snprintf(status_text, sizeof(status_text),
             cluster_view ? "DXCluster window shown"
                          : "DXCluster window hidden");
    if (app_debug_enabled) {
      fprintf(stderr, "[debug] APP_KEY_F5: cluster_view=%d\n",
              cluster_view ? 1 : 0);
    }
  }

  if (key == APP_KEY_F7) {
    cty_update_in_progress = 1;
    snprintf(status_text, sizeof(status_text),
             "Downloading wl_cty.dat... keyboard locked");
    return APP_CTRL_EVENT_REQUEST_CTY_UPDATE;
  }

  if (key == APP_KEY_F1) {
    snprintf(status_text, sizeof(status_text),
             "CALL RST COMMENTS | Space: next field | F2 new | F3 previous");
    return APP_CTRL_EVENT_NONE;
  }

  if (!export_prompt_mode && key == APP_KEY_TAB) {
    if (active_entry_field == ENTRY_FIELD_CALL) {
      const char *selected = call_suggestion_selected(&call_suggestions);
      if (selected) {
        snprintf(entry_call, sizeof(entry_call), "%s", selected);
        update_dxcc_from_input(entry_call);
        refresh_callsign_suggestion(entry_call);
      }
    }
    update_composed_input_line();
    return APP_CTRL_EVENT_NONE;
  }

  if (!export_prompt_mode && active_entry_field == ENTRY_FIELD_CALL &&
      call_suggestion_available && key == APP_KEY_UP) {
    call_suggestion_select_prev(&call_suggestions);
    sync_callsign_suggestion_state();
    return APP_CTRL_EVENT_NONE;
  }

  if (!export_prompt_mode && active_entry_field == ENTRY_FIELD_CALL &&
      call_suggestion_available && key == APP_KEY_DOWN) {
    call_suggestion_select_next(&call_suggestions);
    sync_callsign_suggestion_state();
    return APP_CTRL_EVENT_NONE;
  }

  if (!export_prompt_mode && key == APP_KEY_SPACE) {
    if (active_entry_field == ENTRY_FIELD_CALL && call_suggestion_available) {
      const char *selected = call_suggestion_selected(&call_suggestions);
      if (selected)
        snprintf(entry_call, sizeof(entry_call), "%s", selected);
    }

    advance_entry_field();
    update_dxcc_from_input(entry_call);
    refresh_callsign_suggestion(entry_call);
    update_composed_input_line();
    return APP_CTRL_EVENT_NONE;
  }

  if (key == APP_KEY_BACKSPACE) {
    if (export_prompt_mode) {
      if (input_len > 0)
        input_buffer[--input_len] = 0;
      return APP_CTRL_EVENT_NONE;
    }

    backspace_active_field();
    update_composed_input_line();
    update_dxcc_from_input(entry_call);
    refresh_callsign_suggestion(entry_call);
    return APP_CTRL_EVENT_NONE;
  }

  if (key == APP_KEY_ENTER) {
    if (export_prompt_mode) {
      if (strlen(input_buffer)) {
        if (export_with_adif_filename(input_buffer) != 0)
          snprintf(status_text, sizeof(status_text), "Export failed");
        export_prompt_mode = false;
        input_buffer[0] = 0;
        input_len = 0;
        clear_callsign_suggestion();
      } else {
        snprintf(status_text, sizeof(status_text),
                 "Please enter ADIF filename (Esc to cancel)");
      }

      return APP_CTRL_EVENT_NONE;
    }

    update_composed_input_line();

    if (entry_call[0] || entry_rst[0] || entry_comments[0]) {
      char command_line[256] = {0};
      compose_command_line(command_line, sizeof(command_line));

      if (strcmp(command_line, "quit") == 0) {
        return APP_CTRL_EVENT_EXIT;
      }

      if (!process_command(command_line)) {
        if (entry_call[0] && !entry_rst[0] && !entry_comments[0] &&
            is_digits_only(entry_call)) {
          const int freq_khz = atoi(entry_call);

          if (!is_valid_frequency_khz(freq_khz)) {
            snprintf(status_text, sizeof(status_text), "Invalid frequency");
          } else {
            manual_entry_freq_khz = freq_khz;

            if (cat_is_connected()) {
              if (cat_set_frequency_khz(freq_khz) == 0) {
                snprintf(status_text, sizeof(status_text),
                         "Frequency set to %d kHz (manual + CAT)", freq_khz);
              } else {
                snprintf(status_text, sizeof(status_text),
                         "Frequency set to %d kHz (manual)", freq_khz);
              }
            } else {
              snprintf(status_text, sizeof(status_text),
                       "Frequency set to %d kHz (manual)", freq_khz);
            }
          }
        } else if (entry_call[0] && entry_rst[0] && entry_comments[0] &&
            strlen(entry_rst) >= 4 && is_digits_only(entry_rst) &&
            is_digits_only(entry_comments)) {
          process_qso(command_line);
          call_history_record_from_input(command_line);
        } else if (entry_call[0] && entry_rst[0]) {
          const int qso_freq_khz = resolve_qso_frequency_khz();
          int idx = qso_add_fields(entry_call, qso_freq_khz, entry_rst,
                                   entry_comments, status_text,
                                   sizeof(status_text));
          if (idx >= 0) {
            QSO *q = &logbook[idx];
            snprintf(info_text, sizeof(info_text), "%s %s", q->band, q->mode);

            if (q->country[0] && strcmp(q->country, "UNKNOWN") != 0) {
              snprintf(dxcc_text, sizeof(dxcc_text), "%s", q->country);
              last_cq = q->cq_zone;
              last_itu = q->itu_zone;
            } else {
              snprintf(dxcc_text, sizeof(dxcc_text), "Unknown");
              last_cq = 0;
              last_itu = 0;
            }

            stats_update();
            call_history_record_from_input(entry_call);
          }
        } else {
          snprintf(status_text, sizeof(status_text), "Bad format");
        }
      }
    }

    clear_entry_fields();
    clear_callsign_suggestion();
    return APP_CTRL_EVENT_NONE;
  }

  if (key >= 0 && key <= 255 && isprint(key)) {
    if (export_prompt_mode) {
      if (input_len < (int)sizeof(input_buffer) - 1) {
        input_buffer[input_len++] = (char)key;
        input_buffer[input_len] = 0;
      }
      return APP_CTRL_EVENT_NONE;
    }

    if (append_to_active_field(key)) {
      update_composed_input_line();
      update_dxcc_from_input(entry_call);
      refresh_callsign_suggestion(entry_call);
    }
  }

  if (!export_prompt_mode)
    update_composed_input_line();

  return APP_CTRL_EVENT_NONE;
}