#include "config.h"
#include "cty.h"
#include "dxcluster.h"
#include "export.h"
#include "globals.h"
#include "qso.h"
#include "stats.h"
#include "ui.h"

static char input_buffer[256];

static char status_text[128] = "Ready";
static char dxcc_text[128] = "";
static char info_text[128] = "";

int last_cq = 0;
int last_itu = 0;
int cty_update_in_progress = 0;
int call_suggestion_available = 0;

#define MAX_CALL_HISTORY 20000

static char call_history[MAX_CALL_HISTORY][32];
static int call_history_count = 0;

static char suggested_call[32] = {0};
static bool has_call_suggestion = false;

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
  has_call_suggestion = false;
  call_suggestion_available = 0;
  suggested_call[0] = 0;
}

static void refresh_callsign_suggestion(const char *input) {
  clear_callsign_suggestion();

  if (!input || !input[0])
    return;

  size_t in_len = strlen(input);
  size_t start = 0;

  while (start < in_len && isspace((unsigned char)input[start]))
    start++;

  if (start >= in_len)
    return;

  size_t end = start;
  while (end < in_len && !isspace((unsigned char)input[end]) &&
         input[end] != ';') {
    end++;
  }

  if (end >= in_len)
    ;
  else if (isspace((unsigned char)input[end]))
    return;

  size_t token_len = end - start;
  if (token_len < 1 || token_len >= sizeof(suggested_call))
    return;

  char prefix[32] = {0};
  if (!extract_callsign_token(input, prefix, sizeof(prefix)))
    return;

  for (int i = call_history_count - 1; i >= 0; i--) {
    if (strncmp(call_history[i], prefix, token_len) != 0)
      continue;

    if (strcmp(call_history[i], prefix) == 0)
      continue;

    snprintf(suggested_call, sizeof(suggested_call), "%s", call_history[i]);
    has_call_suggestion = true;
    call_suggestion_available = 1;
    return;
  }
}

static int apply_callsign_suggestion(char *input, int *len) {
  if (!input || !len || !has_call_suggestion)
    return 0;

  size_t in_len = strlen(input);
  size_t start = 0;

  while (start < in_len && isspace((unsigned char)input[start]))
    start++;

  size_t end = start;
  while (end < in_len && !isspace((unsigned char)input[end]) &&
         input[end] != ';') {
    end++;
  }

  size_t sugg_len = strlen(suggested_call);
  size_t suffix_len = in_len - end;

  if (start + sugg_len + suffix_len >= sizeof(input_buffer))
    return 0;

  memmove(input + start + sugg_len, input + end, suffix_len + 1);
  memcpy(input + start, suggested_call, sugg_len);

  *len = (int)strlen(input);

  return 1;
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

static int is_command(const char *s) {
  if (strcmp(s, "quit") == 0)
    return 1;

  if (strncmp(s, "export", 6) == 0)
    return 1;

  if (strcmp(s, "invalid") == 0)
    return 1;

  return 0;
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

int main(void) {
  memset(input_buffer, 0, sizeof(input_buffer));

  if (config_load("logger.conf") != 0) {
    fprintf(stderr, "Cannot load logger.conf\n");
  }

  cty_load("wl_cty.dat");

  qso_init();
  call_history_load_file("call_history.txt");

  ui_init();

  dxcluster_start();

  int len = 0;
  bool cluster_view = false;
  int cluster_scroll = 0;
  bool export_prompt_mode = false;
  char display_info[128];

  while (1) {
    if (has_call_suggestion && !export_prompt_mode)
      snprintf(display_info, sizeof(display_info), "Suggest: %s [Tab]",
               suggested_call);
    else
      snprintf(display_info, sizeof(display_info), "%s", info_text);

    if (cluster_view)
      draw_cluster_fullscreen(cluster_scroll);
    else
      draw_all(input_buffer, status_text, dxcc_text, display_info);

    int ch = wgetch(w_input);
    if (ch == ERR)
      ch = -1;

    if (ch == KEY_RESIZE) {
      ui_resize();
      continue;
    }

    if (ch == KEY_F(10))
      break;

    if (cty_update_in_progress)
      continue;

    if (ch == KEY_F(2)) {
      export_prompt_mode = true;
      clear_callsign_suggestion();
      input_buffer[0] = 0;
      len = 0;
      snprintf(status_text, sizeof(status_text),
               "Enter ADIF filename and press Enter (Esc to cancel)");
      continue;
    }

    if (export_prompt_mode && ch == 27) {
      export_prompt_mode = false;
      input_buffer[0] = 0;
      len = 0;
      snprintf(status_text, sizeof(status_text), "Export cancelled");
      continue;
    }

    if (ch == KEY_F(3)) {
      stats_update();
      snprintf(status_text, sizeof(status_text), "STATS updated");
    }

    if (ch == KEY_F(4)) {
      cluster_view = !cluster_view;
      if (cluster_view) {
        cluster_scroll = 0;
        snprintf(status_text, sizeof(status_text), "DXCluster full view");
      } else {
        snprintf(status_text, sizeof(status_text), "Returned to main view");
      }
    }

    if (ch == KEY_F(5)) {
      cty_update_in_progress = 1;

      snprintf(status_text, sizeof(status_text),
               "Downloading wl_cty.dat... keyboard locked");
      draw_all(input_buffer, status_text, dxcc_text, info_text);

      if (cty_download_latest("wl_cty.dat") == 0) {
        int loaded = cty_load("wl_cty.dat");
        if (loaded >= 0)
          snprintf(status_text, sizeof(status_text), "CTY updated (%d entries)",
                   loaded);
        else
          snprintf(status_text, sizeof(status_text),
                   "Downloaded CTY, reload failed");
      } else {
        snprintf(status_text, sizeof(status_text), "CTY download failed");
      }

      cty_update_in_progress = 0;

      continue;
    }

    if (cluster_view) {
      if (ch == KEY_UP) {
        cluster_scroll = cluster_scroll > 0 ? cluster_scroll - 1 : 0;
        continue;
      }
      if (ch == KEY_DOWN) {
        cluster_scroll++;
        continue;
      }
      if (ch == KEY_PPAGE) {
        cluster_scroll = cluster_scroll > 5 ? cluster_scroll - 5 : 0;
        continue;
      }
      if (ch == KEY_NPAGE) {
        cluster_scroll += 5;
        continue;
      }
      if (ch == KEY_F(10))
        break;
      if (ch == KEY_F(4))
        continue;
      continue;
    }

    if (ch == KEY_F(1)) {
      snprintf(status_text, sizeof(status_text),
               "CALL FREQ RST [MODE] | F2 prompts ADIF filename");
      continue;
    }

    if (!export_prompt_mode && ch == '\t') {
      if (apply_callsign_suggestion(input_buffer, &len)) {
        update_dxcc_from_input(input_buffer);
        refresh_callsign_suggestion(input_buffer);
      }
      continue;
    }

    if (ch == KEY_BACKSPACE || ch == 127) {
      if (len > 0) {
        input_buffer[--len] = 0;
      }

      if (!export_prompt_mode)
        update_dxcc_from_input(input_buffer);

      if (!export_prompt_mode)
        refresh_callsign_suggestion(input_buffer);
      continue;
    }

    if (ch == '\n') {
      if (export_prompt_mode) {
        if (strlen(input_buffer)) {
          if (export_with_adif_filename(input_buffer) != 0)
            snprintf(status_text, sizeof(status_text), "Export failed");
          export_prompt_mode = false;
          input_buffer[0] = 0;
          len = 0;
          clear_callsign_suggestion();
        } else {
          snprintf(status_text, sizeof(status_text),
                   "Please enter ADIF filename (Esc to cancel)");
        }

        continue;
      }

      if (strlen(input_buffer)) {
        if (strncmp(input_buffer, "export", 6) == 0) {
          process_command(input_buffer);
        } else if (strcmp(input_buffer, "quit") == 0) {
          break;
        } else if (strcmp(input_buffer, "invalid") == 0) {
          process_command(input_buffer);
        } else {
          call_history_record_from_input(input_buffer);
          process_qso(input_buffer);
        }
      }

      input_buffer[0] = 0;
      len = 0;
      clear_callsign_suggestion();
    }

    if (isprint(ch)) {
      if (len < (int)sizeof(input_buffer) - 1) {
        input_buffer[len++] = (char)ch;

        input_buffer[len] = 0;
      }

      if (!export_prompt_mode)
        update_dxcc_from_input(input_buffer);

      if (!export_prompt_mode)
        refresh_callsign_suggestion(input_buffer);
    }
  }

  dxcluster_stop();

  ui_shutdown();

  return 0;
}
