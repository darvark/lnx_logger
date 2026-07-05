#ifndef STATS_H
#define STATS_H

#include "qso.h"

typedef struct {
  int total_qso;
  int total_dxcc;

  int cw;
  int ssb;
  int ft8;
  int ft4;
  int rtty;
  int psk31;

} Statistics;

extern Statistics stats;

void stats_update(void);

#endif
