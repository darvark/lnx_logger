#include "stats.h"

Statistics stats;

/* prosta lista DXCC (bez hashmapy – Etap 3) */
static char dxcc_list[MAX_QSO][64];
static int dxcc_count = 0;

/*
 * Check whether a DXCC country already exists in the current set.
 *
 * @param country Country name to search for.
 * @return 1 if present, otherwise 0.
 */
static int dxcc_exists(const char *country) {
  for (int i = 0; i < dxcc_count; i++) {
    if (strcmp(dxcc_list[i], country) == 0)
      return 1;
  }
  return 0;
}

/*
 * Add a DXCC country to the current set if it is valid and new.
 *
 * @param country Country name to add.
 * @return Nothing.
 */
static void dxcc_add(const char *country) {
  if (!country || !country[0])
    return;
  if (strcmp(country, "UNKNOWN") == 0)
    return;

  for (int i = 0; i < dxcc_count; i++)
    if (strcmp(dxcc_list[i], country) == 0)
      return;

  strcpy(dxcc_list[dxcc_count++], country);
}

/*
 * Reset the cached statistics and DXCC set.
 *
 * @return Nothing.
 */
static void reset_stats(void) {
  memset(&stats, 0, sizeof(stats));
  dxcc_count = 0;
}

/*
 * Recalculate aggregate logbook statistics from the in-memory QSO list.
 *
 * @return Nothing.
 */
void stats_update(void) {
  reset_stats();

  for (int i = 0; i < qso_count; i++) {
    QSO *q = &logbook[i];

    if (q->invalid)
      continue;

    stats.total_qso++;

    if (q->country[0])
      dxcc_add(q->country);

    if (strcmp(q->mode, "CW") == 0)
      stats.cw++;
    else if (strcmp(q->mode, "SSB") == 0)
      stats.ssb++;
    else if (strcmp(q->mode, "FT8") == 0)
      stats.ft8++;
    else if (strcmp(q->mode, "FT4") == 0)
      stats.ft4++;
    else if (strcmp(q->mode, "RTTY") == 0)
      stats.rtty++;
    else if (strcmp(q->mode, "PSK31") == 0)
      stats.psk31++;
  }

  stats.total_dxcc = dxcc_count;
}
