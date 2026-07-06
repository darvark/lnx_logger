#include "db.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

#define SQLITE_OK 0
#define SQLITE_ROW 100
#define SQLITE_DONE 101
#define SQLITE_TRANSIENT ((void (*)(void *))-1)

extern int sqlite3_open(const char *filename, sqlite3 **ppDb);
extern int sqlite3_close(sqlite3 *db);
extern int sqlite3_busy_timeout(sqlite3 *db, int ms);
extern int sqlite3_exec(sqlite3 *db, const char *sql, int (*callback)(void *, int, char **, char **), void *arg, char **errmsg);
extern int sqlite3_prepare_v2(sqlite3 *db, const char *zSql, int nByte, sqlite3_stmt **ppStmt, const char **pzTail);
extern int sqlite3_step(sqlite3_stmt *stmt);
extern int sqlite3_finalize(sqlite3_stmt *stmt);
extern int sqlite3_column_int(sqlite3_stmt *stmt, int iCol);
extern long long sqlite3_column_int64(sqlite3_stmt *stmt, int iCol);
extern const unsigned char *sqlite3_column_text(sqlite3_stmt *stmt, int iCol);
extern int sqlite3_bind_text(sqlite3_stmt *stmt, int idx, const char *value, int n, void (*destructor)(void *));
extern int sqlite3_bind_int(sqlite3_stmt *stmt, int idx, int value);
extern int sqlite3_bind_int64(sqlite3_stmt *stmt, int idx, long long value);
extern long long sqlite3_last_insert_rowid(sqlite3 *db);
extern void sqlite3_free(void *ptr);
extern int sqlite3_reset(sqlite3_stmt *stmt);
extern int sqlite3_clear_bindings(sqlite3_stmt *stmt);

static sqlite3 *db = NULL;
static char db_path[512] = {0};
static int db_initialized = 0;
static int db_is_default_path = 1;
static int db_bootstrap_import_done = 0;

static int table_is_empty(const char *table);
static int meta_get_int(const char *key, int *value);
static int meta_set_int(const char *key, int value);

static int exec_sql(const char *sql) {
  char *err = NULL;
  int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
  if (err) {
    sqlite3_free(err);
  }
  return rc;
}

static int prepare_stmt(sqlite3_stmt **stmt, const char *sql) {
  return sqlite3_prepare_v2(db, sql, -1, stmt, NULL);
}

static int import_call_history_file_impl(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f)
    return -1;

  sqlite3_stmt *stmt = NULL;
  if (prepare_stmt(&stmt, "INSERT INTO call_history (call) VALUES (?);") != SQLITE_OK) {
    fclose(f);
    return -1;
  }

  char line[128];
  int imported = 0;

  exec_sql("BEGIN;");

  while (fgets(line, sizeof(line), f)) {
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
      line[n - 1] = 0;
      n--;
    }

    for (size_t i = 0; line[i]; i++)
      line[i] = (char)toupper((unsigned char)line[i]);

    if (!line[0])
      continue;

    sqlite3_bind_text(stmt, 1, line, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_DONE)
      imported++;
    else if (rc != SQLITE_ROW && rc != SQLITE_DONE)
      break;

    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
  }

  sqlite3_finalize(stmt);
  fclose(f);
  exec_sql("COMMIT;");

  return imported;
}

static int ensure_open(void) {
  if (db)
    return 0;

  const char *env_path = getenv("LOGGER_DB_PATH");
  const char *path = (env_path && env_path[0]) ? env_path : "logger.db";

  snprintf(db_path, sizeof(db_path), "%s", path);
  db_is_default_path = !(env_path && env_path[0]);

  if (sqlite3_open(db_path, &db) != SQLITE_OK) {
    if (db) {
      sqlite3_close(db);
      db = NULL;
    }
    return -1;
  }

  sqlite3_busy_timeout(db, 2000);
  exec_sql("PRAGMA foreign_keys = ON;");

  if (exec_sql(
          "CREATE TABLE IF NOT EXISTS qso ("
          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "date TEXT NOT NULL,"
          "utc TEXT NOT NULL,"
          "call TEXT NOT NULL,"
          "freq INTEGER NOT NULL,"
          "band TEXT NOT NULL,"
          "mode TEXT NOT NULL,"
          "rst TEXT NOT NULL,"
          "country TEXT NOT NULL,"
          "cq_zone INTEGER NOT NULL,"
          "itu_zone INTEGER NOT NULL,"
          "invalid INTEGER NOT NULL DEFAULT 0"
          ");") != SQLITE_OK)
    return -1;

  if (exec_sql(
          "CREATE TABLE IF NOT EXISTS call_history ("
          "id INTEGER PRIMARY KEY AUTOINCREMENT,"
          "call TEXT NOT NULL"
          ");") != SQLITE_OK)
    return -1;

  if (exec_sql(
          "CREATE TABLE IF NOT EXISTS app_meta ("
          "key TEXT PRIMARY KEY,"
          "value INTEGER NOT NULL"
          ");") != SQLITE_OK)
    return -1;

  if (db_is_default_path && !db_bootstrap_import_done) {
    int imported_flag = 0;
    if (meta_get_int("call_history_bootstrap", &imported_flag) != 0 ||
        imported_flag == 0) {
      if (table_is_empty("call_history") &&
          import_call_history_file_impl("call_history.txt") >= 0) {
        meta_set_int("call_history_bootstrap", 1);
      }
    }

    db_bootstrap_import_done = 1;
  }

  return 0;
}

static int table_is_empty(const char *table) {
  char sql[128];
  snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s;", table);

  sqlite3_stmt *stmt = NULL;
  if (prepare_stmt(&stmt, sql) != SQLITE_OK)
    return 0;

  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW)
    count = sqlite3_column_int(stmt, 0);

  sqlite3_finalize(stmt);
  return count == 0;
}

static int meta_get_int(const char *key, int *value) {
  if (!key || !key[0] || !value)
    return -1;

  sqlite3_stmt *stmt = NULL;
  if (prepare_stmt(&stmt, "SELECT value FROM app_meta WHERE key = ?;") != SQLITE_OK)
    return -1;

  sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *value = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return 0;
  }

  sqlite3_finalize(stmt);
  return -1;
}

static int meta_set_int(const char *key, int value) {
  if (!key || !key[0])
    return -1;

  sqlite3_stmt *stmt = NULL;
  if (prepare_stmt(&stmt,
                   "INSERT INTO app_meta (key, value) VALUES (?, ?) "
                   "ON CONFLICT(key) DO UPDATE SET value = excluded.value;") != SQLITE_OK)
    return -1;

  sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, value);
  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return rc == SQLITE_DONE ? 0 : -1;
}

void db_shutdown(void) {
  if (db) {
    sqlite3_close(db);
    db = NULL;
  }

  db_initialized = 0;
}

int db_init(void) {
  if (db_initialized && db)
    return 0;

  if (ensure_open() != 0)
    return -1;

  db_initialized = 1;
  return 0;
}

int db_load_qsos(QSO *logbook, int max_qso, long long *ids, int *out_count) {
  if (out_count)
    *out_count = 0;

  if (!logbook || max_qso <= 0)
    return -1;

  if (db_init() != 0)
    return -1;

  sqlite3_stmt *stmt = NULL;
  if (prepare_stmt(&stmt,
                   "SELECT id,date,utc,call,freq,band,mode,rst,country,cq_zone,itu_zone,invalid "
                   "FROM qso ORDER BY id ASC;") != SQLITE_OK)
    return -1;

  int count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW && count < max_qso) {
    QSO *q = &logbook[count];
    memset(q, 0, sizeof(*q));

    q->db_id = sqlite3_column_int64(stmt, 0);
    snprintf(q->date, sizeof(q->date), "%s", (const char *)sqlite3_column_text(stmt, 1));
    snprintf(q->utc, sizeof(q->utc), "%s", (const char *)sqlite3_column_text(stmt, 2));
    snprintf(q->call, sizeof(q->call), "%s", (const char *)sqlite3_column_text(stmt, 3));
    q->freq = sqlite3_column_int(stmt, 4);
    snprintf(q->band, sizeof(q->band), "%s", (const char *)sqlite3_column_text(stmt, 5));
    snprintf(q->mode, sizeof(q->mode), "%s", (const char *)sqlite3_column_text(stmt, 6));
    snprintf(q->rst, sizeof(q->rst), "%s", (const char *)sqlite3_column_text(stmt, 7));
    snprintf(q->country, sizeof(q->country), "%s", (const char *)sqlite3_column_text(stmt, 8));
    q->cq_zone = sqlite3_column_int(stmt, 9);
    q->itu_zone = sqlite3_column_int(stmt, 10);
    q->invalid = sqlite3_column_int(stmt, 11) != 0;

    if (ids)
      ids[count] = q->db_id;

    count++;
  }

  sqlite3_finalize(stmt);

  if (out_count)
    *out_count = count;

  return 0;
}

int db_insert_qso(const QSO *qso, long long *out_id) {
  if (out_id)
    *out_id = 0;

  if (!qso)
    return -1;

  if (db_init() != 0)
    return -1;

  sqlite3_stmt *stmt = NULL;
  if (prepare_stmt(&stmt,
                   "INSERT INTO qso (date,utc,call,freq,band,mode,rst,country,cq_zone,itu_zone,invalid) "
                   "VALUES (?,?,?,?,?,?,?,?,?,?,?);") != SQLITE_OK)
    return -1;

  sqlite3_bind_text(stmt, 1, qso->date, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, qso->utc, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, qso->call, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 4, qso->freq);
  sqlite3_bind_text(stmt, 5, qso->band, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, qso->mode, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, qso->rst, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 8, qso->country, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 9, qso->cq_zone);
  sqlite3_bind_int(stmt, 10, qso->itu_zone);
  sqlite3_bind_int(stmt, 11, qso->invalid ? 1 : 0);

  int rc = sqlite3_step(stmt);
  if (rc != SQLITE_DONE) {
    sqlite3_finalize(stmt);
    return -1;
  }

  if (out_id)
    *out_id = sqlite3_last_insert_rowid(db);

  sqlite3_finalize(stmt);
  return 0;
}

int db_update_qso_invalid(long long id, int invalid) {
  if (id <= 0)
    return -1;

  if (db_init() != 0)
    return -1;

  sqlite3_stmt *stmt = NULL;
  if (prepare_stmt(&stmt, "UPDATE qso SET invalid = ? WHERE id = ?;") != SQLITE_OK)
    return -1;

  sqlite3_bind_int(stmt, 1, invalid ? 1 : 0);
  sqlite3_bind_int64(stmt, 2, id);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  return rc == SQLITE_DONE ? 0 : -1;
}

int db_load_call_history(char history[][32], int max_history, int *out_count) {
  if (out_count)
    *out_count = 0;

  if (!history || max_history <= 0)
    return -1;

  if (db_init() != 0)
    return -1;

  sqlite3_stmt *stmt = NULL;
  if (prepare_stmt(&stmt, "SELECT call FROM call_history ORDER BY id ASC;") != SQLITE_OK)
    return -1;

  int count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW && count < max_history) {
    const unsigned char *call = sqlite3_column_text(stmt, 0);
    if (call && call[0]) {
      snprintf(history[count], 32, "%s", call);
      count++;
    }
  }

  sqlite3_finalize(stmt);

  if (out_count)
    *out_count = count;

  return 0;
}

int db_append_call_history(const char *call) {
  if (!call || !call[0])
    return -1;

  if (db_init() != 0)
    return -1;

  sqlite3_stmt *stmt = NULL;
  if (prepare_stmt(&stmt, "INSERT INTO call_history (call) VALUES (?);") != SQLITE_OK)
    return -1;

  sqlite3_bind_text(stmt, 1, call, -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  return rc == SQLITE_DONE ? 0 : -1;
}

int db_import_call_history_file(const char *path) {
  if (!path || !path[0])
    return -1;

  if (db_init() != 0)
    return -1;

  return import_call_history_file_impl(path);
}

int db_clear_logbook(void) {
  if (db_init() != 0)
    return -1;

  if (exec_sql("BEGIN;") != SQLITE_OK)
    return -1;

  int ok = exec_sql("DELETE FROM qso;") == SQLITE_OK &&
           exec_sql("DELETE FROM call_history;") == SQLITE_OK &&
           exec_sql("DELETE FROM sqlite_sequence WHERE name IN ('qso','call_history');") == SQLITE_OK;

  if (exec_sql(ok ? "COMMIT;" : "ROLLBACK;") != SQLITE_OK)
    return -1;

  return ok ? 0 : -1;
}

static int export_qso_rows(const char *sql, FILE *f, int adif_mode) {
  sqlite3_stmt *stmt = NULL;
  if (prepare_stmt(&stmt, sql) != SQLITE_OK)
    return -1;

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    const char *date = (const char *)sqlite3_column_text(stmt, 0);
    const char *utc = (const char *)sqlite3_column_text(stmt, 1);
    const char *call = (const char *)sqlite3_column_text(stmt, 2);
    int freq = sqlite3_column_int(stmt, 3);
    const char *band = (const char *)sqlite3_column_text(stmt, 4);
    const char *mode = (const char *)sqlite3_column_text(stmt, 5);
    const char *rst = (const char *)sqlite3_column_text(stmt, 6);
    const char *country = (const char *)sqlite3_column_text(stmt, 7);

    if (!adif_mode) {
      fprintf(f, "%s,%s,%s,%d,%s,%s,%s,%s\n", date, utc, call, freq, band,
              mode, rst, country);
    } else {
      fprintf(f, "<CALL:%zu>%s", strlen(call), call);
      fprintf(f, "<QSO_DATE:8>%s", date);
      fprintf(f, "<TIME_ON:4>%s", utc);
      fprintf(f, "<FREQ:9>%.6f", freq / 1000.0);
      fprintf(f, "<BAND:%zu>%s", strlen(band), band);
      fprintf(f, "<MODE:%zu>%s", strlen(mode), mode);
      fprintf(f, "<RST_SENT:%zu>%s", strlen(rst), rst);
      fprintf(f, "<RST_RCVD:%zu>%s", strlen(rst), rst);
      if (country && country[0])
        fprintf(f, "<COUNTRY:%zu>%s", strlen(country), country);
      fprintf(f, "<EOR>\n");
    }
  }

  sqlite3_finalize(stmt);
  return 0;
}

int db_export_csv(const char *filename) {
  FILE *f = fopen(filename, "w");
  if (!f)
    return -1;

  if (db_init() != 0) {
    fclose(f);
    return -1;
  }

  fprintf(f, "DATE,UTC,CALL,FREQ,BAND,MODE,RST,COUNTRY\n");

  int rc = export_qso_rows(
      "SELECT date,utc,call,freq,band,mode,rst,country FROM qso WHERE invalid = 0 ORDER BY id ASC;",
      f, 0);

  fclose(f);
  return rc;
}

int db_export_adif(const char *filename) {
  FILE *f = fopen(filename, "w");
  if (!f)
    return -1;

  if (db_init() != 0) {
    fclose(f);
    return -1;
  }

  fprintf(f, "Generated by Logger\n");
  fprintf(f, "<EOH>\n");

  int rc = export_qso_rows(
      "SELECT date,utc,call,freq,band,mode,rst,country FROM qso WHERE invalid = 0 ORDER BY id ASC;",
      f, 1);

  fclose(f);
  return rc;
}