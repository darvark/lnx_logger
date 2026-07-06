#include "app_controller.h"

#include "config.h"
#include "cty.h"
#include "dxcluster.h"
#include "export.h"
#include "globals.h"
#include "qso.h"
#include "suggestion.h"
#include "stats.h"

static char input_buffer[256];
static int input_len = 0;

static char status_text[128] = "Ready";
static char dxcc_text[128] = "";
static char info_text[128] = "";
static char display_info[128] = "";

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

static bool cluster_view = false;
static int cluster_scroll = 0;
static bool export_prompt_mode = false;

static void sync_callsign_suggestion_state(void);

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

static void call_history_append_file(const char *call) {
  FILE *f = fopen("call_history.txt", "a");
  if (!f)
    return;

  fprintf(f, "%s\n", call);
  fclose(f);
}

static void call_history_record_from_input(const char *input) {
  char call[32];

  if (!extract_callsign_token(input, call, sizeof(call)))
    return;

  if (strcmp(call, "EXPORT") == 0 || strcmp(call, "INVALID") == 0 ||
      strcmp(call, "QUIT") == 0)
    return;

  call_history_add_memory(call);
  call_history_append_file(call);
}

static void call_history_load_file(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f)
    return;

  char line[128];

  while (fgets(line, sizeof(line), f)) {
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
      line[n - 1] = 0;
      n--;
    }

    for (size_t i = 0; line[i]; i++)
      line[i] = (char)toupper((unsigned char)line[i]);

    if (line[0])
      call_history_add_memory(line);
  }

  fclose(f);
}

static void clear_callsign_suggestion(void) {
  call_suggestion_list_clear(&call_suggestions);
  call_suggestion_available = 0;
  call_suggestion_count = 0;
  call_suggestion_selected_index = 0;

  for (int i = 0; i < CALL_SUGGESTION_MAX; i++)
    call_suggestion_matches[i][0] = 0;
}

static void refresh_callsign_suggestion(const char *input) {
  clear_callsign_suggestion();

  call_suggestion_refresh(&call_suggestions, input, call_history,
                          call_history_count);
  sync_callsign_suggestion_state();
}

static int apply_callsign_suggestion(char *input, int *len) {
  return call_suggestion_apply(&call_suggestions, input, len,
                               sizeof(input_buffer));
}

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

static void process_command(const char *cmd) {
  if (strncmp(cmd, "export", 6) == 0) {
    if (export_with_optional_adif(cmd) != 0)
      snprintf(status_text, sizeof(status_text), "Export failed");

    return;
  }

  if (strcmp(cmd, "invalid") == 0) {
    if (qso_count > 0) {
      qso_mark_invalid(qso_count - 1);

      snprintf(status_text, sizeof(status_text), "Last QSO toggled INVALID");
    }

    return;
  }
}

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

int app_controller_init(void) {
  memset(input_buffer, 0, sizeof(input_buffer));
  input_len = 0;

  if (config_load("logger.conf") != 0)
    fprintf(stderr, "Cannot load logger.conf\n");

  cty_load("wl_cty.dat");
  qso_init();
  call_history_load_file("call_history.txt");

  dxcluster_start();
  update_display_info();

  return 0;
}

void app_controller_shutdown(void) {
  dxcluster_stop();
}

void app_controller_get_render_state(AppRenderState *out) {
  if (!out)
    return;

  update_display_info();

  out->input = input_buffer;
  out->status = status_text;
  out->dxcc = dxcc_text;
  out->info = display_info;
  out->cluster_view = cluster_view;
  out->cluster_scroll = cluster_scroll;
}

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

AppControllerEvent app_controller_handle_key(int key) {
  if (key == APP_KEY_NONE || key == APP_KEY_RESIZE)
    return APP_CTRL_EVENT_NONE;

  if (key == APP_KEY_F10)
    return APP_CTRL_EVENT_EXIT;

  if (cty_update_in_progress)
    return APP_CTRL_EVENT_NONE;

  if (key == APP_KEY_F2) {
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

  if (key == APP_KEY_F3) {
    stats_update();
    snprintf(status_text, sizeof(status_text), "STATS updated");
  }

  if (key == APP_KEY_F4) {
    cluster_view = !cluster_view;
    if (cluster_view) {
      cluster_scroll = 0;
      snprintf(status_text, sizeof(status_text), "DXCluster full view");
    } else {
      snprintf(status_text, sizeof(status_text), "Returned to main view");
    }
  }

  if (key == APP_KEY_F5) {
    cty_update_in_progress = 1;
    snprintf(status_text, sizeof(status_text),
             "Downloading wl_cty.dat... keyboard locked");
    return APP_CTRL_EVENT_REQUEST_CTY_UPDATE;
  }

  if (cluster_view) {
    if (key == APP_KEY_UP) {
      cluster_scroll = cluster_scroll > 0 ? cluster_scroll - 1 : 0;
      return APP_CTRL_EVENT_NONE;
    }
    if (key == APP_KEY_DOWN) {
      cluster_scroll++;
      return APP_CTRL_EVENT_NONE;
    }
    if (key == APP_KEY_PAGE_UP) {
      cluster_scroll = cluster_scroll > 5 ? cluster_scroll - 5 : 0;
      return APP_CTRL_EVENT_NONE;
    }
    if (key == APP_KEY_PAGE_DOWN) {
      cluster_scroll += 5;
      return APP_CTRL_EVENT_NONE;
    }

    return APP_CTRL_EVENT_NONE;
  }

  if (key == APP_KEY_F1) {
    snprintf(status_text, sizeof(status_text),
             "CALL FREQ RST [MODE] | F2 prompts ADIF filename");
    return APP_CTRL_EVENT_NONE;
  }

  if (!export_prompt_mode && key == APP_KEY_TAB) {
    if (apply_callsign_suggestion(input_buffer, &input_len)) {
      update_dxcc_from_input(input_buffer);
      refresh_callsign_suggestion(input_buffer);
    }
    return APP_CTRL_EVENT_NONE;
  }

  if (!export_prompt_mode && call_suggestion_available && key == APP_KEY_UP) {
    call_suggestion_select_prev(&call_suggestions);
    sync_callsign_suggestion_state();
    return APP_CTRL_EVENT_NONE;
  }

  if (!export_prompt_mode && call_suggestion_available && key == APP_KEY_DOWN) {
    call_suggestion_select_next(&call_suggestions);
    sync_callsign_suggestion_state();
    return APP_CTRL_EVENT_NONE;
  }

  if (!export_prompt_mode && call_suggestion_available && key == APP_KEY_SPACE) {
    if (apply_callsign_suggestion(input_buffer, &input_len)) {
      if (input_len < (int)sizeof(input_buffer) - 1 &&
          (input_len == 0 || input_buffer[input_len - 1] != ' ')) {
        input_buffer[input_len++] = ' ';
        input_buffer[input_len] = 0;
      }
    }

    update_dxcc_from_input(input_buffer);
    refresh_callsign_suggestion(input_buffer);
    return APP_CTRL_EVENT_NONE;
  }

  if (key == APP_KEY_BACKSPACE) {
    if (input_len > 0)
      input_buffer[--input_len] = 0;

    if (!export_prompt_mode)
      update_dxcc_from_input(input_buffer);

    if (!export_prompt_mode)
      refresh_callsign_suggestion(input_buffer);
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

    if (strlen(input_buffer)) {
      if (strncmp(input_buffer, "export", 6) == 0) {
        process_command(input_buffer);
      } else if (strcmp(input_buffer, "quit") == 0) {
        return APP_CTRL_EVENT_EXIT;
      } else if (strcmp(input_buffer, "invalid") == 0) {
        process_command(input_buffer);
      } else {
        call_history_record_from_input(input_buffer);
        process_qso(input_buffer);
      }
    }

    input_buffer[0] = 0;
    input_len = 0;
    clear_callsign_suggestion();
    return APP_CTRL_EVENT_NONE;
  }

  if (key >= 0 && key <= 255 && isprint(key)) {
    if (input_len < (int)sizeof(input_buffer) - 1) {
      input_buffer[input_len++] = (char)key;
      input_buffer[input_len] = 0;
    }

    if (!export_prompt_mode)
      update_dxcc_from_input(input_buffer);

    if (!export_prompt_mode)
      refresh_callsign_suggestion(input_buffer);
  }

  return APP_CTRL_EVENT_NONE;
}