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

  ui_init();

  dxcluster_start();

  int len = 0;
  bool cluster_view = false;
  int cluster_scroll = 0;
  bool export_prompt_mode = false;

  while (1) {
    if (cluster_view)
      draw_cluster_fullscreen(cluster_scroll);
    else
      draw_all(input_buffer, status_text, dxcc_text, info_text);

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

    if (ch == KEY_BACKSPACE || ch == 127) {
      if (len > 0) {
        input_buffer[--len] = 0;
      }

      if (!export_prompt_mode)
        update_dxcc_from_input(input_buffer);
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
          process_qso(input_buffer);
        }
      }

      input_buffer[0] = 0;
      len = 0;
    }

    if (isprint(ch)) {
      if (len < (int)sizeof(input_buffer) - 1) {
        input_buffer[len++] = (char)ch;

        input_buffer[len] = 0;
      }

      if (!export_prompt_mode)
        update_dxcc_from_input(input_buffer);
    }
  }

  dxcluster_stop();

  ui_shutdown();

  return 0;
}
