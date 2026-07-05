#include "ui.h"

WINDOW *w_log = NULL;
WINDOW *w_input = NULL;
WINDOW *w_status = NULL;
WINDOW *w_dxcc = NULL;
WINDOW *w_info = NULL;
WINDOW *w_cluster = NULL;
WINDOW *w_func = NULL;
WINDOW *w_stats = NULL;

static int log_h;
static int input_h;
static int status_h;
static int dxcc_h;
static int info_h;
static int stats_h;
static int func_h;
static int cluster_h;

static void create_windows(void) {
  int rows, cols;

  getmaxyx(stdscr, rows, cols);

  log_h = 12;
  input_h = 3;
  status_h = 1;
  dxcc_h = 1;
  info_h = 1;
  stats_h = 2;
  func_h = 1;

  cluster_h =
      rows - log_h - input_h - status_h - dxcc_h - info_h - stats_h - func_h;

  if (cluster_h < 6)
    cluster_h = 6;

  if (cluster_h > 20)
    cluster_h = 20;

  int y = 0;

  w_log = newwin(log_h, cols, y, 0);
  y += log_h;

  w_input = newwin(input_h, cols, y, 0);
  wbkgd(w_input, COLOR_PAIR(3));
  y += input_h;

  w_status = newwin(status_h, cols, y, 0);
  wbkgd(w_status, COLOR_PAIR(4));
  y += status_h;

  w_dxcc = newwin(dxcc_h, cols, y, 0);
  y += dxcc_h;

  w_info = newwin(info_h, cols, y, 0);
  y += info_h;

  w_stats = newwin(stats_h, cols, y, 0);
  y += stats_h;

  w_cluster = newwin(cluster_h, cols, y, 0);
  y += cluster_h;

  w_func = newwin(func_h, cols, y, 0);

  keypad(stdscr, TRUE);
  keypad(w_input, TRUE);
}

static void destroy_windows(void) {
  if (w_log)
    delwin(w_log);
  if (w_input)
    delwin(w_input);
  if (w_status)
    delwin(w_status);
  if (w_dxcc)
    delwin(w_dxcc);
  if (w_info)
    delwin(w_info);
  if (w_stats)
    delwin(w_stats);
  if (w_cluster)
    delwin(w_cluster);
  if (w_func)
    delwin(w_func);

  w_log = NULL;
  w_input = NULL;
  w_status = NULL;
  w_dxcc = NULL;
  w_info = NULL;
  w_stats = NULL;
  w_cluster = NULL;
  w_func = NULL;
}

void ui_init(void) {
  initscr();
  cbreak();
  noecho();
  curs_set(1);

  keypad(stdscr, TRUE);

  if (has_colors()) {
    start_color();

    init_pair(1, COLOR_CYAN, -1);
    init_pair(2, COLOR_YELLOW, -1);
    init_pair(3, COLOR_GREEN, COLOR_BLACK); // INPUT FIX
    init_pair(4, COLOR_BLACK, COLOR_CYAN);
    init_pair(5, COLOR_MAGENTA, -1);
    init_pair(6, COLOR_RED, -1);
    init_pair(7, COLOR_YELLOW, -1);
  }

  create_windows();

  keypad(w_input, TRUE);
  nodelay(w_input, TRUE);
}

void ui_shutdown(void) {
  destroy_windows();
  endwin();
}

void ui_resize(void) {
  endwin();
  refresh();
  clear();

  ui_init();
}

/* ------------------------------------------------ */

void draw_log(void) {
  werase(w_log);
  box(w_log, 0, 0);

  wattron(w_log, COLOR_PAIR(1) | A_BOLD);

  mvwprintw(w_log, 0, 2, " QSO Log ");

  mvwprintw(w_log, 1, 1,
            "Nr Date     UTC  Call           Freq   Band Mode RST");

  wattroff(w_log, COLOR_PAIR(1) | A_BOLD);

  int visible = getmaxy(w_log) - 3;

  int start = qso_count > visible ? qso_count - visible : 0;

  int row = 2;

  if (qso_count == 0) {
    mvwprintw(w_log, 2, 2, "No QSOs");
    return;
  }

  for (int i = start; i < qso_count; i++) {
    if (logbook[i].invalid)
      wattron(w_log, COLOR_PAIR(6) | A_BOLD);

    mvwprintw(w_log, row++, 1, "%2d %-8s %-4s %-14s %5d %-4s %-4s %-3s", i + 1,
              logbook[i].date, logbook[i].utc, logbook[i].call, logbook[i].freq,
              logbook[i].band, logbook[i].mode, logbook[i].rst);

    if (logbook[i].invalid)
      wattroff(w_log, COLOR_PAIR(6) | A_BOLD);
  }
}

/* ------------------------------------------------ */

void draw_input(const char *buf) {
  werase(w_input);
  wbkgd(w_input, COLOR_PAIR(3));
  box(w_input, 0, 0);

  wattron(w_input, A_BOLD);

  mvwprintw(w_input, 1, 2, "CALL FREQ RST > %s", buf);

  wattroff(w_input, A_BOLD);
}

/* ------------------------------------------------ */

void draw_status(const char *text) {
  werase(w_status);

  mvwprintw(w_status, 0, 1, "Status: %s", text);
}

void draw_dxcc(const char *text) {
  werase(w_dxcc);

  wattron(w_dxcc, COLOR_PAIR(2) | A_BOLD);

  mvwprintw(w_dxcc, 0, 1, "DXCC: %-20s CQ:%d ITU:%d", text, last_cq, last_itu);

  wattroff(w_dxcc, COLOR_PAIR(2) | A_BOLD);
}

void draw_info(const char *text) {
  werase(w_info);

  wattron(w_info, COLOR_PAIR(5));

  mvwprintw(w_info, 0, 1, "Info: %s", text);

  wattroff(w_info, COLOR_PAIR(5));
}

/* ------------------------------------------------ */

void draw_stats(void) {
  werase(w_stats);

  mvwprintw(w_stats, 0, 1, "QSO:%d DXCC:%d", stats.total_qso, stats.total_dxcc);

  mvwprintw(w_stats, 1, 1, "CW:%d SSB:%d FT8:%d FT4:%d RTTY:%d PSK31:%d",
            stats.cw, stats.ssb, stats.ft8, stats.ft4, stats.rtty, stats.psk31);
}

/* ------------------------------------------------ */

void draw_cluster(void) {
  werase(w_cluster);
  box(w_cluster, 0, 0);

  wattron(w_cluster, COLOR_PAIR(1) | A_BOLD);

  mvwprintw(w_cluster, 0, 2, " DX Cluster ");

  pthread_mutex_lock(&dxcluster_mutex);
  mvwprintw(w_cluster, 0, 18, "[%s]", dxcluster_status);

  int visible = getmaxy(w_cluster) - 2;
  if (visible < 1)
    visible = 1;

  int count = spot_count;
  int first = spot_start;
  if (count > visible)
    first = (spot_start + count - visible) % MAX_SPOTS;

  int row = 1;
  for (int i = 0; i < count && row < getmaxy(w_cluster); i++) {
    int idx = (first + i) % MAX_SPOTS;
    wattron(w_cluster, COLOR_PAIR(7) | A_BOLD);
    mvwprintw(w_cluster, row++, 1, "%-8s %-10s %-12s %s", spots[idx].time,
              spots[idx].freq, spots[idx].call, spots[idx].comment);
    wattroff(w_cluster, COLOR_PAIR(7) | A_BOLD);
  }
  pthread_mutex_unlock(&dxcluster_mutex);

  wattroff(w_cluster, COLOR_PAIR(1) | A_BOLD);
}

/* ------------------------------------------------ */

void draw_cluster_fullscreen(int scroll) {
  werase(stdscr);
  box(stdscr, 0, 0);

  wattron(stdscr, COLOR_PAIR(1) | A_BOLD);
  mvwprintw(stdscr, 0, 2, " DX Cluster Full View ");
  wattroff(stdscr, COLOR_PAIR(1) | A_BOLD);

  pthread_mutex_lock(&dxcluster_mutex);
  mvwprintw(stdscr, 0, 24, "[%s]", dxcluster_status);

  int rows = getmaxy(stdscr);
  int visible = rows - 4;
  if (visible < 1)
    visible = 1;

  int count = spot_count;
  int max_scroll = count > visible ? count - visible : 0;
  if (scroll > max_scroll)
    scroll = max_scroll;
  if (scroll < 0)
    scroll = 0;

  int start = 0;
  if (count > visible)
    start = count - visible - scroll;

  int row = 2;
  for (int i = start; i < count && row < rows - 1; i++) {
    int idx = (spot_start + i) % MAX_SPOTS;
    wattron(stdscr, COLOR_PAIR(7) | A_BOLD);
    mvwprintw(stdscr, row++, 1, "%-8s %-10s %-12s %s", spots[idx].time,
              spots[idx].freq, spots[idx].call, spots[idx].comment);
    wattroff(stdscr, COLOR_PAIR(7) | A_BOLD);
  }

  mvwprintw(stdscr, rows - 1, 1,
            "UP/DOWN scroll  PgUp/PgDn page  F4 return  %d/%d", scroll + 1,
            max_scroll + 1);

  pthread_mutex_unlock(&dxcluster_mutex);
  refresh();
}

/* ------------------------------------------------ */

void draw_function_bar(void) {
  werase(w_func);

  wattron(w_func, A_REVERSE);

  if (cty_update_in_progress) {
    mvwprintw(w_func, 0, 1,
              "F5 CTY update in progress... keyboard locked");
  } else {
    mvwprintw(
        w_func, 0, 1,
        "F1 Help  F2 Export  F3 Stats  F4 Cluster  F5 CTY update  F10 Quit");
  }

  wattroff(w_func, A_REVERSE);
}

/* ------------------------------------------------ */

void draw_all(const char *input, const char *status, const char *dxcc,
              const char *info) {
  draw_log();
  draw_input(input);
  draw_status(status);
  draw_dxcc(dxcc);
  draw_info(info);
  draw_stats();
  draw_cluster();
  draw_function_bar();

  wnoutrefresh(w_log);
  wnoutrefresh(w_input);
  wnoutrefresh(w_status);
  wnoutrefresh(w_dxcc);
  wnoutrefresh(w_info);
  wnoutrefresh(w_stats);
  wnoutrefresh(w_cluster);
  wnoutrefresh(w_func);

  doupdate();
}
