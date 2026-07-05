#ifndef CONFIG_H
#define CONFIG_H

#include "globals.h"

typedef struct {
  double lat;
  double lon;

  char locator[16];

  char dxc_host[128];
  int dxc_port;
  char dxc_call[32];

} Config;

extern Config config;

int config_load(const char *filename);

#endif
