#ifndef CTY_H
#define CTY_H

#include "globals.h"

typedef struct {
  char prefix[16];
  char country[64];

  int cq_zone;
  int itu_zone;

  double lat;
  double lon;

} CtyEntry;

int cty_load(const char *filename);
int cty_download_latest(const char *filename);

const CtyEntry *cty_lookup(const char *callsign);

#endif
