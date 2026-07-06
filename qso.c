#include "qso.h"

#include "db.h"

QSO logbook[MAX_QSO];
int qso_count = 0;

/* ------------------------------------------------ */

static int validate_callsign(const char *call) {
  if (!call)
    return 0;

  size_t len = strlen(call);

  if (len < 3 || len > 20)
    return 0;

  int has_digit = 0;

  for (size_t i = 0; i < len; i++) {
    char c = toupper(call[i]);

    if (isdigit((unsigned char)c))
      has_digit = 1;

    if (!(isalnum((unsigned char)c) || c == '/'))
      return 0;
  }

  return has_digit;
}

/* ------------------------------------------------ */

static int validate_frequency(int freq) {
  if (freq < 1000)
    return 0;

  if (freq > 500000)
    return 0;

  return 1;
}

/* ------------------------------------------------ */

static int validate_rst(const char *rst) {
  if (!rst)
    return 0;

  size_t len = strlen(rst);

  if (len < 2 || len > 3)
    return 0;

  for (size_t i = 0; i < len; i++) {
    if (!isdigit((unsigned char)rst[i]))
      return 0;
  }

  return 1;
}

/* ------------------------------------------------ */

void detect_band(int freq, char *band) {
  if (freq >= 1800 && freq <= 2000)
    strcpy(band, "160M");

  else if (freq >= 3500 && freq <= 4000)
    strcpy(band, "80M");

  else if (freq >= 7000 && freq <= 7300)
    strcpy(band, "40M");

  else if (freq >= 10100 && freq <= 10150)
    strcpy(band, "30M");

  else if (freq >= 14000 && freq <= 14350)
    strcpy(band, "20M");

  else if (freq >= 18068 && freq <= 18168)
    strcpy(band, "17M");

  else if (freq >= 21000 && freq <= 21450)
    strcpy(band, "15M");

  else if (freq >= 24890 && freq <= 24990)
    strcpy(band, "12M");

  else if (freq >= 28000 && freq <= 29700)
    strcpy(band, "10M");

  else if (freq >= 50000 && freq <= 54000)
    strcpy(band, "6M");

  else if (freq >= 144000 && freq <= 148000)
    strcpy(band, "2M");

  else
    strcpy(band, "?");
}

/* ------------------------------------------------ */

void detect_mode(int freq, char *mode) {
  if (freq == 14074 || freq == 7074 || freq == 3573 || freq == 21074) {
    strcpy(mode, "FT8");
  } else if (freq == 14080 || freq == 7047 || freq == 3575) {
    strcpy(mode, "FT4");
  } else if (freq >= 14070 && freq <= 14071) {
    strcpy(mode, "PSK31");
  } else if (freq >= 14080 && freq <= 14099) {
    strcpy(mode, "RTTY");
  } else if (freq < 14100) {
    strcpy(mode, "CW");
  } else {
    strcpy(mode, "SSB");
  }
}

/* ------------------------------------------------ */

void qso_init(void) {
  memset(logbook, 0, sizeof(logbook));
  qso_count = 0;

  if (db_init() != 0)
    return;

  int loaded = 0;
  if (db_load_qsos(logbook, MAX_QSO, NULL, &loaded) == 0)
    qso_count = loaded;
}

/* ------------------------------------------------ */

static void fill_utc(QSO *q) {
  time_t t = time(NULL);

  struct tm *utc = gmtime(&t);

  strftime(q->date, sizeof(q->date), "%Y%m%d", utc);

  strftime(q->utc, sizeof(q->utc), "%H%M", utc);
}

/* ------------------------------------------------ */

int qso_add(const char *line, char *status, size_t status_size) {
  if (!line)
    return -1;

  char buf[256];
  strncpy(buf, line, sizeof(buf));
  buf[sizeof(buf) - 1] = 0;

  char *call = strtok(buf, " ");
  char *freq = strtok(NULL, " ");
  char *rst = strtok(NULL, " ");
  char *mode = strtok(NULL, " ");

  if (!call || !freq || !rst) {
    snprintf(status, status_size, "Bad format");
    return -1;
  }

  for (int i = 0; call[i]; i++)
    call[i] = toupper(call[i]);

  char *p = strchr(call, '/');
  if (p)
    *p = 0;

  if (!validate_callsign(call)) {
    snprintf(status, status_size, "Invalid callsign");
    return -1;
  }

  int f = atoi(freq);

  if (qso_count >= MAX_QSO) {
    snprintf(status, status_size, "Logbook full");
    return -1;
  }

  QSO q;
  memset(&q, 0, sizeof(q));

  strcpy(q.call, call);
  strcpy(q.rst, rst);
  q.freq = f;

  detect_band(f, q.band);
  detect_mode(f, q.mode);

  fill_utc(&q);

  const CtyEntry *cty = cty_lookup(call);

  if (cty) {
    strncpy(q.country, cty->country, sizeof(q.country) - 1);
    q.cq_zone = cty->cq_zone;
    q.itu_zone = cty->itu_zone;
  } else {
    strcpy(q.country, "UNKNOWN");
  }

  long long db_id = 0;
  if (db_insert_qso(&q, &db_id) == 0)
    q.db_id = db_id;

  logbook[qso_count] = q;
  qso_count++;

  snprintf(status, status_size, "QSO OK");
  return qso_count - 1;
}

/* ------------------------------------------------ */

void qso_mark_invalid(int index) {
  if (index < 0)
    return;

  if (index >= qso_count)
    return;

  logbook[index].invalid = !logbook[index].invalid;
  db_update_qso_invalid(logbook[index].db_id, logbook[index].invalid);
}
