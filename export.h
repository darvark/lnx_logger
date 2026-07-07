#ifndef EXPORT_H
#define EXPORT_H

/*
 * Export the current logbook to CSV, using the database layer when available.
 *
 * @param filename Output file path.
 * @return 0 on success, or -1 on failure.
 */
int export_csv(const char *filename);

/*
 * Export the current logbook to ADIF, using the database layer when available.
 *
 * @param filename Output file path.
 * @return 0 on success, or -1 on failure.
 */
int export_adif(const char *filename);

#endif
