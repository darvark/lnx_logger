#include "config.h"
#include "cty.h"
#include "dxcluster.h"
#include "export.h"
#include "maidenhead.h"
#include "qso.h"
#include "stats.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int g_failures = 0;

static void failf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "[FAIL] ");
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  g_failures++;
}

static void expect_true(int condition, const char *message) {
  if (!condition)
    failf("%s", message);
}

static void expect_int_eq(int actual, int expected, const char *message) {
  if (actual != expected)
    failf("%s (actual=%d expected=%d)", message, actual, expected);
}

static void expect_str_eq(const char *actual, const char *expected,
                          const char *message) {
  if (!actual || strcmp(actual, expected) != 0)
    failf("%s (actual='%s' expected='%s')", message, actual ? actual : "(null)",
          expected ? expected : "(null)");
}

static void expect_double_close(double actual, double expected, double eps,
                                const char *message) {
  if (fabs(actual - expected) > eps)
    failf("%s (actual=%.8f expected=%.8f eps=%.8f)", message, actual, expected,
          eps);
}

static int write_text_file(const char *path, const char *text) {
  FILE *f = fopen(path, "w");
  if (!f)
    return -1;

  fputs(text, f);
  fclose(f);
  return 0;
}

static char *read_whole_file(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f)
    return NULL;

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }

  long size = ftell(f);
  if (size < 0) {
    fclose(f);
    return NULL;
  }

  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }

  char *buf = malloc((size_t)size + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }

  size_t n = fread(buf, 1, (size_t)size, f);
  fclose(f);

  buf[n] = 0;
  return buf;
}

static int make_temp_dir(char *out, size_t out_size) {
  const char *tmp_base = getenv("TMPDIR");

  if (!tmp_base || !tmp_base[0])
    tmp_base = "/tmp";

  if (!out || out_size < 32)
    return -1;

  snprintf(out, out_size, "%s/lnx_logger_unit_XXXXXX", tmp_base);
  if (!mkdtemp(out))
    return -1;

  return 0;
}

static void test_config_load(const char *tmp_dir) {
  char conf_path[512];

  snprintf(conf_path, sizeof(conf_path), "%s/logger.conf", tmp_dir);

  const char *conf_text =
      "# Unit config\n"
      " LAT = 52.2297  \n"
      "LON=21.0122\n"
      "LOCATOR = JO92AA\n"
      "DXC_HOST = dx.example.net\n"
      "DXC_PORT = 9000\n"
      "DXC_CALL = SP9XYZ\n";

  expect_int_eq(write_text_file(conf_path, conf_text), 0,
                "write unit logger.conf");

  expect_int_eq(config_load(conf_path), 0, "config_load should succeed");
  expect_double_close(config.lat, 52.2297, 0.0001, "config LAT parsed");
  expect_double_close(config.lon, 21.0122, 0.0001, "config LON parsed");
  expect_str_eq(config.locator, "JO92AA", "config locator parsed");
  expect_str_eq(config.dxc_host, "dx.example.net", "config host parsed");
  expect_int_eq(config.dxc_port, 9000, "config port parsed");
  expect_str_eq(config.dxc_call, "SP9XYZ", "config call parsed");

  expect_int_eq(config_load("/definitely/missing/logger.conf"), -1,
                "missing config should return -1");
  expect_str_eq(config.dxc_host, "telnet.reversebeacon.net",
                "default host restored on missing config");
  expect_int_eq(config.dxc_port, 7000,
                "default port restored on missing config");
  expect_str_eq(config.dxc_call, "N0CALL",
                "default call restored on missing config");
}

static void test_cty_load_and_lookup(const char *tmp_dir) {
  char cty_path[512];
  snprintf(cty_path, sizeof(cty_path), "%s/wl_cty.dat", tmp_dir);

  const char *cty_text =
      "Poland:15:28:EU:52.0:21.0:0:SP:\n"
      "SP,HF;\n"
      "United States:5:8:NA:38.0:-97.0:0:K:\n"
      "K;\n"
      "United States K1:5:8:NA:41.0:-71.0:0:K1:\n"
      "K1;\n";

  expect_int_eq(write_text_file(cty_path, cty_text), 0, "write test CTY file");

  int loaded = cty_load(cty_path);
  expect_true(loaded > 0, "cty_load should load entries");

  const CtyEntry *sp = cty_lookup("sp9abc");
  expect_true(sp != NULL, "SP9ABC should resolve");
  if (sp) {
    expect_str_eq(sp->country, "Poland", "SP9ABC country");
    expect_int_eq(sp->cq_zone, 15, "SP9ABC CQ zone");
    expect_int_eq(sp->itu_zone, 28, "SP9ABC ITU zone");
  }

  const CtyEntry *k1 = cty_lookup("K1ABC");
  expect_true(k1 != NULL, "K1ABC should resolve");
  if (k1) {
    expect_str_eq(k1->country, "United States K1",
                  "longest prefix K1 should win");
  }

  const CtyEntry *unknown = cty_lookup("ZZ9ZZZ");
  expect_true(unknown == NULL, "Unknown prefix should not resolve");
}

static void test_cty_download_latest_failure_path(const char *tmp_dir) {
  char cty_path[512];
  snprintf(cty_path, sizeof(cty_path), "%s/downloaded_wl_cty.dat", tmp_dir);

  const char *old_path = getenv("PATH");
  char old_path_buf[2048] = {0};

  if (old_path)
    snprintf(old_path_buf, sizeof(old_path_buf), "%s", old_path);

  setenv("PATH", "", 1);

  int rc = cty_download_latest(cty_path);
  expect_int_eq(rc, -1,
                "cty_download_latest should fail when curl/wget are unavailable");

  if (old_path)
    setenv("PATH", old_path_buf, 1);
  else
    unsetenv("PATH");
}

static void test_qso_helpers(void) {
  char band[8] = {0};
  char mode[16] = {0};

  detect_band(14074, band);
  expect_str_eq(band, "20M", "detect_band 14074");

  detect_band(144100, band);
  expect_str_eq(band, "2M", "detect_band 144100");

  detect_band(999, band);
  expect_str_eq(band, "?", "detect_band unknown");

  detect_mode(14074, mode);
  expect_str_eq(mode, "FT8", "detect_mode FT8");

  detect_mode(14080, mode);
  expect_str_eq(mode, "FT4", "detect_mode FT4 exact");

  detect_mode(14071, mode);
  expect_str_eq(mode, "PSK31", "detect_mode PSK31");

  detect_mode(14090, mode);
  expect_str_eq(mode, "RTTY", "detect_mode RTTY");

  detect_mode(7020, mode);
  expect_str_eq(mode, "CW", "detect_mode CW");

  detect_mode(14150, mode);
  expect_str_eq(mode, "SSB", "detect_mode SSB");
}

static void test_qso_add_mark_and_stats(void) {
  char status[128];

  qso_init();
  expect_int_eq(qso_count, 0, "qso_init resets qso_count");

  int idx1 = qso_add("SP9ABC 14074 599", status, sizeof(status));
  expect_int_eq(idx1, 0, "first QSO index");
  expect_str_eq(status, "QSO OK", "first QSO status");
  expect_str_eq(logbook[0].call, "SP9ABC", "callsign normalized");
  expect_str_eq(logbook[0].band, "20M", "band assigned");
  expect_str_eq(logbook[0].mode, "FT8", "mode assigned");

  int idx2 = qso_add("K1ABC 14150 59", status, sizeof(status));
  expect_int_eq(idx2, 1, "second QSO index");
  expect_str_eq(logbook[1].mode, "SSB", "second mode assigned");

  int bad_format = qso_add("K1ABC 14150", status, sizeof(status));
  expect_int_eq(bad_format, -1, "bad format rejected");
  expect_str_eq(status, "Bad format", "bad format status");

  int bad_call = qso_add("ABCDEF 14074 599", status, sizeof(status));
  expect_int_eq(bad_call, -1, "invalid call rejected");
  expect_str_eq(status, "Invalid callsign", "invalid call status");

  qso_mark_invalid(-1);
  qso_mark_invalid(99);

  qso_mark_invalid(1);
  expect_true(logbook[1].invalid, "qso_mark_invalid toggles on");

  qso_mark_invalid(1);
  expect_true(!logbook[1].invalid, "qso_mark_invalid toggles off");

  qso_mark_invalid(1);
  stats_update();

  expect_int_eq(stats.total_qso, 1, "stats total excludes invalid");
  expect_int_eq(stats.total_dxcc, 1, "stats DXCC excludes invalid");
  expect_int_eq(stats.ft8, 1, "stats FT8 count");
  expect_int_eq(stats.ssb, 0, "stats SSB count");

  qso_mark_invalid(1);
  stats_update();

  expect_int_eq(stats.total_qso, 2, "stats total after re-enable");
  expect_int_eq(stats.total_dxcc, 2, "stats DXCC after re-enable");
  expect_int_eq(stats.ssb, 1, "stats SSB after re-enable");
}

static void test_export_csv_adif(const char *tmp_dir) {
  char csv_path[512];
  char adif_path[512];

  snprintf(csv_path, sizeof(csv_path), "%s/unit_log.csv", tmp_dir);
  snprintf(adif_path, sizeof(adif_path), "%s/unit_log.adi", tmp_dir);

  qso_mark_invalid(1);

  expect_int_eq(export_csv(csv_path), 0, "export_csv should succeed");
  expect_int_eq(export_adif(adif_path), 0, "export_adif should succeed");

  char *csv = read_whole_file(csv_path);
  char *adi = read_whole_file(adif_path);

  expect_true(csv != NULL, "CSV output should be readable");
  expect_true(adi != NULL, "ADIF output should be readable");

  if (csv) {
    expect_true(strstr(csv, "DATE,UTC,CALL,FREQ,BAND,MODE,RST,COUNTRY") != NULL,
                "CSV header exists");
    expect_true(strstr(csv, "SP9ABC") != NULL, "CSV contains SP9ABC");
    expect_true(strstr(csv, "K1ABC") == NULL,
                "CSV excludes invalid entries");
  }

  if (adi) {
    expect_true(strstr(adi, "<EOH>") != NULL, "ADIF header exists");
    expect_true(strstr(adi, "<CALL:6>SP9ABC") != NULL,
                "ADIF contains SP9ABC");
    expect_true(strstr(adi, "<CALL:5>K1ABC") == NULL,
                "ADIF excludes invalid entries");
  }

  free(csv);
  free(adi);

  qso_mark_invalid(1);
}

static void test_maidenhead(void) {
  double lat = 0.0;
  double lon = 0.0;

  expect_int_eq(locator_to_latlon("JO90", &lat, &lon), 0,
                "locator JO90 should parse");
  expect_double_close(lat, 50.5, 0.001, "JO90 latitude");
  expect_double_close(lon, 19.0, 0.001, "JO90 longitude");

  expect_int_eq(locator_to_latlon("JO90aa", &lat, &lon), 0,
                "locator JO90aa should parse");
  expect_double_close(lat, 50.020833, 0.001, "JO90aa latitude");
  expect_double_close(lon, 18.041666, 0.001, "JO90aa longitude");

  expect_int_eq(locator_to_latlon("ZZ99", &lat, &lon), -1,
                "invalid locator should fail");
  expect_int_eq(locator_to_latlon(NULL, &lat, &lon), -1,
                "NULL locator should fail");
}

static void test_dxcluster_set_status(void) {
  expect_int_eq(config_load("/definitely/missing/logger.conf"), -1,
                "missing config returns -1 but applies defaults");

  dxcluster_set_status("Connected");
  expect_true(strstr(dxcluster_status, "Connected") != NULL,
              "status should include message");
  expect_true(strstr(dxcluster_status, "telnet.reversebeacon.net") != NULL,
              "status should include default host");
  expect_true(strstr(dxcluster_status, ":7000") != NULL,
              "status should include default port");

  snprintf(config.dxc_host, sizeof(config.dxc_host), "%s", "cluster.local");
  config.dxc_port = 7300;

  dxcluster_set_status("Ready");
  expect_true(strstr(dxcluster_status, "Ready") != NULL,
              "custom status message applied");
  expect_true(strstr(dxcluster_status, "cluster.local") != NULL,
              "custom host reflected in status");
  expect_true(strstr(dxcluster_status, ":7300") != NULL,
              "custom port reflected in status");
}

static void test_dxcluster_start_stop(void) {
  snprintf(config.dxc_host, sizeof(config.dxc_host), "%s", "127.0.0.1");
  config.dxc_port = 9;
  snprintf(config.dxc_call, sizeof(config.dxc_call), "%s", "N0CALL");

  int rc = dxcluster_start();
  expect_int_eq(rc, 0, "dxcluster_start should create thread");

  usleep(50000);

  dxcluster_stop();

  expect_true(strstr(dxcluster_status, "Disconnected") != NULL ||
                  strstr(dxcluster_status, "failed") != NULL ||
                  strstr(dxcluster_status, "timeout") != NULL ||
                  strstr(dxcluster_status, "Connecting") != NULL,
              "dxcluster_stop should finish worker lifecycle");
}

int main(void) {
  char tmp_dir[256];
  if (make_temp_dir(tmp_dir, sizeof(tmp_dir)) != 0) {
    fprintf(stderr, "Cannot create temp dir: %s\n", strerror(errno));
    return 2;
  }

  test_config_load(tmp_dir);
  test_cty_load_and_lookup(tmp_dir);
  test_cty_download_latest_failure_path(tmp_dir);
  test_qso_helpers();
  test_qso_add_mark_and_stats();
  test_export_csv_adif(tmp_dir);
  test_maidenhead();
  test_dxcluster_set_status();
  test_dxcluster_start_stop();

  if (g_failures == 0) {
    printf("All unit tests passed.\n");
    return 0;
  }

  printf("Unit tests failed: %d\n", g_failures);
  return 1;
}
