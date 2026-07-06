#ifndef DB_H
#define DB_H

#include "qso.h"

int db_init(void);
void db_shutdown(void);

int db_load_qsos(QSO *logbook, int max_qso, long long *ids, int *out_count);
int db_insert_qso(const QSO *qso, long long *out_id);
int db_update_qso_invalid(long long id, int invalid);

int db_load_call_history(char history[][32], int max_history, int *out_count);
int db_append_call_history(const char *call);
int db_import_call_history_file(const char *path);

int db_export_csv(const char *filename);
int db_export_adif(const char *filename);

#endif