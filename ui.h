#ifndef UI_H
#define UI_H

#include "dxcluster.h"
#include "globals.h"
#include "qso.h"
#include "stats.h"

#include <ncurses.h>

extern WINDOW *w_log;
extern WINDOW *w_input;
extern WINDOW *w_status;
extern WINDOW *w_dxcc;
extern WINDOW *w_info;
extern WINDOW *w_cluster;
extern WINDOW *w_func;
extern WINDOW *w_stats;
extern WINDOW *w_suggest;

void ui_init(void);
void ui_shutdown(void);
void ui_resize(void);

void draw_log(void);
void draw_input(const char *buf);
void draw_status(const char *text);
void draw_dxcc(const char *text);
void draw_info(const char *text);
void draw_cluster(void);
void draw_suggestions(void);
void draw_cluster_fullscreen(int scroll);
void draw_stats(void);
void draw_function_bar(void);

void draw_all(const char *input, const char *status, const char *dxcc,
              const char *info);

#endif
