#ifndef QSO_H
#define QSO_H

#include "cty.h"
#include "globals.h"

#define MAX_QSO 1000

typedef struct {
  long long db_id;

  char date[9];
  char utc[5];

  char call[32];

  int freq;

  char band[8];
  char mode[16];
  char rst[8];

  char country[64];
  int cq_zone;
  int itu_zone;

  bool invalid;

} QSO;

extern QSO logbook[MAX_QSO];
extern int qso_count;

void qso_init(void);

int qso_add(const char *line, char *status, size_t status_size);

void qso_mark_invalid(int index);

void detect_band(int freq, char *band);

void detect_mode(int freq, char *mode);

#endif
