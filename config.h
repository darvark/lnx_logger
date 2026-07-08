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

  int cat_model;
  char cat_device[128];
  int cat_baud;
  int cat_data_bits;
  int cat_stop_bits;
  char cat_parity[16];
  char cat_handshake[16];

} Config;

extern Config config;

/*
 * Load the logger configuration file into the global config structure.
 *
 * @param filename Path to the configuration file to read.
 * @return 0 on success, or -1 if the file cannot be opened.
 */
int config_load(const char *filename);

#endif
