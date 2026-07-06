#include "ui.h"

WINDOW *w_log = NULL;
WINDOW *w_input = NULL;
WINDOW *w_status = NULL;
WINDOW *w_dxcc = NULL;
WINDOW *w_info = NULL;
WINDOW *w_cluster = NULL;
WINDOW *w_func = NULL;
WINDOW *w_stats = NULL;
WINDOW *w_suggest = NULL;
WINDOW *w_gap = NULL;

static int log_h;
static int input_h;
static int status_h;
static int dxcc_h;
static int info_h;
static int stats_h;
static int func_h;
static int cluster_h;

#define STATS_CLUSTER_GAP 1

static void create_windows(void) {
  int rows, cols;

  getmaxyx(stdscr, rows, cols);

  log_h = 12;
  input_h = 3;
  status_h = 1;
  dxcc_h = 1;
  info_h = 0;
  stats_h = 1;
  func_h = 1;

  cluster_h =
      rows - log_h - input_h - status_h - dxcc_h - info_h - stats_h - func_h -
      STATS_CLUSTER_GAP;

  if (cluster_h < 6)
    cluster_h = 6;

  if (cluster_h > 20)
    cluster_h = 20;

  int y = 0;

  w_log = newwin(log_h, cols, y, 0);
  wbkgd(w_log, COLOR_PAIR(5));
  y += log_h;

  w_input = newwin(input_h, cols, y, 0);
  wbkgd(w_input, COLOR_PAIR(3));
  y += input_h;

  w_status = newwin(status_h, cols, y, 0);
  wbkgd(w_status, COLOR_PAIR(4));
  y += status_h;

  w_dxcc = newwin(dxcc_h, cols, y, 0);
  wbkgd(w_dxcc, COLOR_PAIR(4));
  y += dxcc_h;

  w_stats = newwin(stats_h, cols, y, 0);
  wbkgd(w_stats, COLOR_PAIR(4));
  y += stats_h;

  w_gap = newwin(STATS_CLUSTER_GAP, cols, y, 0);
  wbkgd(w_gap, COLOR_PAIR(5));

  y += STATS_CLUSTER_GAP;

  w_cluster = newwin(cluster_h, cols, y, 0);
  wbkgd(w_cluster, COLOR_PAIR(5));
  y += cluster_h;

  w_func = newwin(func_h, cols, y, 0);

  int suggest_w = cols / 3;
  if (suggest_w < 24)
    suggest_w = 24;
  if (suggest_w > 40)
    suggest_w = 40;

  int suggest_h = 8;
  if (suggest_h > log_h)
    suggest_h = log_h;

  if (suggest_w >= cols - 2)
    suggest_w = cols - 2;

  if (suggest_w > 0)
    w_suggest = newwin(suggest_h, suggest_w, 0, cols - suggest_w);

  if (w_suggest)
    wbkgd(w_suggest, COLOR_PAIR(5));

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
  if (w_suggest)
    delwin(w_suggest);
  if (w_gap)
    delwin(w_gap);

  w_log = NULL;
  w_input = NULL;
  w_status = NULL;
  w_dxcc = NULL;
  w_info = NULL;
  w_stats = NULL;
  w_cluster = NULL;
  w_func = NULL;
  w_suggest = NULL;
  w_gap = NULL;
}

void ui_init(void) {
  initscr();
  cbreak();
  noecho();
  curs_set(1);

  keypad(stdscr, TRUE);

  if (has_colors()) {
    start_color();

    init_pair(1, COLOR_WHITE, COLOR_BLUE);
    init_pair(2, COLOR_YELLOW, COLOR_BLUE);
    init_pair(3, COLOR_BLACK, COLOR_CYAN);
    init_pair(4, COLOR_WHITE, COLOR_BLUE);
    init_pair(5, COLOR_WHITE, COLOR_CYAN);
    init_pair(6, COLOR_YELLOW, COLOR_CYAN);
    init_pair(7, COLOR_GREEN, COLOR_CYAN);
    init_pair(8, COLOR_BLACK, COLOR_YELLOW);
    init_pair(9, COLOR_BLUE, COLOR_CYAN);
    init_pair(10, COLOR_BLACK, COLOR_YELLOW);
  }

  bkgd(COLOR_PAIR(5));

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

  wattron(w_log, COLOR_PAIR(9));
  box(w_log, 0, 0);
  wattroff(w_log, COLOR_PAIR(9));

  wattron(w_log, COLOR_PAIR(1) | A_BOLD);
  mvwhline(w_log, 0, 1, ' ', getmaxx(w_log) - 2);

  const char *title = "QSO Log";
  int cols = getmaxx(w_log);
  int title_x = (cols - (int)strlen(title)) / 2;
  if (title_x < 1)
    title_x = 1;

  mvwprintw(w_log, 0, title_x, "%s", title);

  wattroff(w_log, COLOR_PAIR(1) | A_BOLD);

  wattron(w_log, COLOR_PAIR(2) | A_BOLD);

  mvwprintw(w_log, 1, 1,
            "Nr Date     UTC  Call           Freq   Band Mode RST");

  wattroff(w_log, COLOR_PAIR(2) | A_BOLD);

  int visible = getmaxy(w_log) - 3;

  int start = qso_count > visible ? qso_count - visible : 0;

  int row = 2;

  if (qso_count == 0) {
    wattron(w_log, COLOR_PAIR(5));
    mvwprintw(w_log, 2, 2, "No QSOs");
    wattroff(w_log, COLOR_PAIR(5));
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
  int input_pair = call_suggestion_available ? 8 : 3;

  werase(w_input);
  wbkgd(w_input, COLOR_PAIR(input_pair));

  wattron(w_input, COLOR_PAIR(1));
  box(w_input, 0, 0);
  wattroff(w_input, COLOR_PAIR(1));

  wattron(w_input, COLOR_PAIR(input_pair) | A_BOLD);

  mvwprintw(w_input, 1, 2, "CALL FREQ RST > %s", buf);

  wattroff(w_input, COLOR_PAIR(input_pair) | A_BOLD);
}

/* ------------------------------------------------ */

void draw_status(const char *text) {
  werase(w_status);
  wbkgd(w_status, COLOR_PAIR(4));

  wattron(w_status, COLOR_PAIR(4) | A_BOLD);

  mvwprintw(w_status, 0, 1, "Status: %s", text);

  wattroff(w_status, COLOR_PAIR(4) | A_BOLD);
}

void draw_dxcc(const char *text) {
  werase(w_dxcc);

  wattron(w_dxcc, COLOR_PAIR(2) | A_BOLD);

  mvwprintw(w_dxcc, 0, 1, "DXCC: %-20s CQ:%d ITU:%d", text, last_cq, last_itu);

  wattroff(w_dxcc, COLOR_PAIR(2) | A_BOLD);
}

void draw_info(const char *text) {
  if (!w_status)
    return;

  char info_buf[256];
  snprintf(info_buf, sizeof(info_buf), "Info: %s", text ? text : "");

  int cols = getmaxx(w_status);
  int info_len = (int)strlen(info_buf);
  int info_x = cols - info_len - 2;
  if (info_x < cols / 2)
    info_x = cols / 2;

  wattron(w_status, COLOR_PAIR(4) | A_BOLD);
  mvwprintw(w_status, 0, info_x, "%.*s", cols - info_x - 1, info_buf);
  wattroff(w_status, COLOR_PAIR(4) | A_BOLD);
}

/* ------------------------------------------------ */

void draw_stats(void) {
  werase(w_stats);
  wbkgd(w_stats, COLOR_PAIR(4));

  wattron(w_stats, COLOR_PAIR(4));
  mvwhline(w_stats, 0, 0, ' ', getmaxx(w_stats));
  wattroff(w_stats, COLOR_PAIR(4));

  wattron(w_stats, COLOR_PAIR(2) | A_BOLD);
  mvwprintw(w_stats, 0, 1,
            "QSO:%d DXCC:%d  CW:%d SSB:%d FT8:%d FT4:%d RTTY:%d PSK31:%d",
            stats.total_qso, stats.total_dxcc,
            stats.cw, stats.ssb, stats.ft8, stats.ft4, stats.rtty, stats.psk31);
  wattroff(w_stats, COLOR_PAIR(2) | A_BOLD);
}

/* ------------------------------------------------ */

void draw_cluster(void) {
  werase(w_cluster);

  wattron(w_cluster, COLOR_PAIR(9));
  box(w_cluster, 0, 0);
  wattroff(w_cluster, COLOR_PAIR(9));

  wattron(w_cluster, COLOR_PAIR(1) | A_BOLD);
  mvwhline(w_cluster, 0, 1, ' ', getmaxx(w_cluster) - 2);

  mvwprintw(w_cluster, 0, 2, " DX Cluster ");

  wattroff(w_cluster, COLOR_PAIR(1) | A_BOLD);

  pthread_mutex_lock(&dxcluster_mutex);

  wattron(w_cluster, COLOR_PAIR(2) | A_BOLD);
  mvwprintw(w_cluster, 0, 18, "[%s]", dxcluster_status);
  wattroff(w_cluster, COLOR_PAIR(2) | A_BOLD);

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
}

/* ------------------------------------------------ */

void draw_suggestions(void) {
  if (!w_suggest)
    return;

  if (!call_suggestion_available || call_suggestion_count <= 0)
    return;

  werase(w_suggest);

  wattron(w_suggest, COLOR_PAIR(9));
  box(w_suggest, 0, 0);
  wattroff(w_suggest, COLOR_PAIR(9));

  wattron(w_suggest, COLOR_PAIR(1) | A_BOLD);
  mvwhline(w_suggest, 0, 1, ' ', getmaxx(w_suggest) - 2);
  mvwprintw(w_suggest, 0, 2, " Call Suggestions ");
  wattroff(w_suggest, COLOR_PAIR(1) | A_BOLD);

  int visible = getmaxy(w_suggest) - 2;
  int content_w = getmaxx(w_suggest) - 4;
  if (content_w < 6)
    content_w = 6;

  for (int i = 0; i < call_suggestion_count && i < visible; i++) {
    if (i == call_suggestion_selected_index)
      wattron(w_suggest, COLOR_PAIR(8) | A_BOLD);
    else
      wattron(w_suggest, COLOR_PAIR(5));

    mvwprintw(w_suggest, i + 1, 2, "%-*.*s", content_w, content_w,
              call_suggestion_matches[i]);

    if (i == call_suggestion_selected_index)
      wattroff(w_suggest, COLOR_PAIR(8) | A_BOLD);
    else
      wattroff(w_suggest, COLOR_PAIR(5));
  }

}

/* ------------------------------------------------ */

void draw_cluster_fullscreen(int scroll) {
  werase(stdscr);

  wattron(stdscr, COLOR_PAIR(9));
  box(stdscr, 0, 0);
  wattroff(stdscr, COLOR_PAIR(9));

  wattron(stdscr, COLOR_PAIR(1) | A_BOLD);
  mvwhline(stdscr, 0, 1, ' ', getmaxx(stdscr) - 2);
  mvwprintw(stdscr, 0, 2, " DX Cluster Full View ");
  wattroff(stdscr, COLOR_PAIR(1) | A_BOLD);

  pthread_mutex_lock(&dxcluster_mutex);

  wattron(stdscr, COLOR_PAIR(2) | A_BOLD);
  mvwprintw(stdscr, 0, 24, "[%s]", dxcluster_status);
  wattroff(stdscr, COLOR_PAIR(2) | A_BOLD);

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

  wattron(stdscr, COLOR_PAIR(2) | A_BOLD);
  mvwprintw(stdscr, rows - 1, 1,
            "UP/DOWN scroll  PgUp/PgDn page  F4 return  %d/%d", scroll + 1,
            max_scroll + 1);
  wattroff(stdscr, COLOR_PAIR(2) | A_BOLD);

  pthread_mutex_unlock(&dxcluster_mutex);
  refresh();
}

/* ------------------------------------------------ */

void draw_function_bar(void) {
  werase(w_func);
  wbkgd(w_func, COLOR_PAIR(cty_update_in_progress ? 10 : 4));

  wattron(w_func, COLOR_PAIR(cty_update_in_progress ? 10 : 4) | A_BOLD);

  if (cty_update_in_progress) {
    mvwprintw(w_func, 0, 1,
              "F5 CTY update in progress... keyboard locked");
  } else {
    mvwprintw(
        w_func, 0, 1,
        "F1 Help  F2 Export  F3 Stats  F4 Cluster  F5 CTY update  F10 Quit");
  }

  wattroff(w_func, COLOR_PAIR(cty_update_in_progress ? 10 : 4) | A_BOLD);
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
  draw_suggestions();

  if (w_gap)
    werase(w_gap);

  wnoutrefresh(w_log);
  wnoutrefresh(w_input);
  wnoutrefresh(w_status);
  wnoutrefresh(w_dxcc);
  wnoutrefresh(w_stats);
  if (w_gap)
    wnoutrefresh(w_gap);
  wnoutrefresh(w_cluster);
  wnoutrefresh(w_func);

  if (call_suggestion_available && call_suggestion_count > 0)
    wnoutrefresh(w_suggest);

  doupdate();
}
