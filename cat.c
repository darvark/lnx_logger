#include "cat.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_HAMLIB
#include <hamlib/rig.h>
#endif

static pthread_mutex_t cat_mutex = PTHREAD_MUTEX_INITIALIZER;
static char cat_status[128] = "CAT idle";

#ifdef HAVE_HAMLIB
static RIG *active_rig = NULL;
static int hamlib_ready = 0;
#endif

static void cat_set_status(const char *text) {
  if (!text)
    return;

  pthread_mutex_lock(&cat_mutex);
  snprintf(cat_status, sizeof(cat_status), "%s", text);
  pthread_mutex_unlock(&cat_mutex);
}

void cat_get_status(char *out, size_t out_size) {
  if (!out || out_size < 2)
    return;

  pthread_mutex_lock(&cat_mutex);
  snprintf(out, out_size, "%s", cat_status);
  pthread_mutex_unlock(&cat_mutex);
}

#ifdef HAVE_HAMLIB
static void cat_set_status_fmt(const char *prefix, int rc) {
  char text[128];
  snprintf(text, sizeof(text), "%s: %s", prefix ? prefix : "CAT error",
           rigerror(rc));
  cat_set_status(text);
}

static void cat_apply_conf(RIG *rig, const char *name, const char *value) {
  if (!rig || !name || !value || !value[0])
    return;

  token_t tok = rig_token_lookup(rig, name);
  if (tok == RIG_CONF_END)
    return;

  rig_set_conf(rig, tok, value);
}

typedef struct {
  CatRigInfo *out;
  int max_entries;
  int count;
} CatRigListCtx;

static int cat_rig_list_cb(const struct rig_caps *caps, rig_ptr_t data) {
  CatRigListCtx *ctx = (CatRigListCtx *)data;
  if (!ctx || !caps)
    return 1;

  if (ctx->count >= ctx->max_entries)
    return 1;

  if (caps->rig_model <= 0 || !caps->model_name)
    return 1;

  CatRigInfo *entry = &ctx->out[ctx->count++];
  entry->model = (int)caps->rig_model;
  snprintf(entry->model_name, sizeof(entry->model_name), "%s %s",
           caps->mfg_name ? caps->mfg_name : "", caps->model_name);

  return 1;
}
#endif

int cat_init(void) {
#ifdef HAVE_HAMLIB
  rig_set_debug(RIG_DEBUG_NONE);
  rig_load_all_backends();
  hamlib_ready = 1;
  cat_set_status("CAT ready");
  return 0;
#else
  cat_set_status("CAT unavailable (built without Hamlib)");
  return -1;
#endif
}

void cat_shutdown(void) {
  cat_disconnect();
}

int cat_list_rigs(CatRigInfo *out, int max_entries) {
  if (!out || max_entries <= 0)
    return 0;

#ifdef HAVE_HAMLIB
  if (!hamlib_ready)
    return 0;

  CatRigListCtx ctx;
  ctx.out = out;
  ctx.max_entries = max_entries;
  ctx.count = 0;

  rig_list_foreach(cat_rig_list_cb, (rig_ptr_t)&ctx);
  return ctx.count;
#else
  return 0;
#endif
}

int cat_connect(const CatConnectionParams *params) {
  if (!params)
    return -1;

#ifdef HAVE_HAMLIB
  if (!hamlib_ready) {
    cat_set_status("CAT not initialized");
    return -1;
  }

  pthread_mutex_lock(&cat_mutex);

  if (active_rig) {
    rig_close(active_rig);
    rig_cleanup(active_rig);
    active_rig = NULL;
  }

  RIG *rig = rig_init((rig_model_t)params->model);
  if (!rig) {
    pthread_mutex_unlock(&cat_mutex);
    cat_set_status("CAT init failed: unknown rig model");
    return -1;
  }

  char baud_text[24];
  char data_bits_text[8];
  char stop_bits_text[8];

  snprintf(baud_text, sizeof(baud_text), "%d", params->baud_rate);
  snprintf(data_bits_text, sizeof(data_bits_text), "%d", params->data_bits);
  snprintf(stop_bits_text, sizeof(stop_bits_text), "%d", params->stop_bits);

  cat_apply_conf(rig, "rig_pathname", params->device);
  cat_apply_conf(rig, "serial_speed", baud_text);
  cat_apply_conf(rig, "data_bits", data_bits_text);
  cat_apply_conf(rig, "stop_bits", stop_bits_text);
  cat_apply_conf(rig, "serial_parity", params->parity);
  cat_apply_conf(rig, "serial_handshake", params->handshake);

  const int rc = rig_open(rig);
  if (rc != RIG_OK) {
    rig_cleanup(rig);
    pthread_mutex_unlock(&cat_mutex);
    cat_set_status_fmt("CAT connect failed", rc);
    return -1;
  }

  active_rig = rig;

  snprintf(cat_status, sizeof(cat_status), "CAT connected (%.96s)",
           params->device[0] ? params->device : "default");
  pthread_mutex_unlock(&cat_mutex);

  return 0;
#else
  (void)params;
  cat_set_status("CAT unavailable (built without Hamlib)");
  return -1;
#endif
}

void cat_disconnect(void) {
#ifdef HAVE_HAMLIB
  pthread_mutex_lock(&cat_mutex);

  if (active_rig) {
    rig_close(active_rig);
    rig_cleanup(active_rig);
    active_rig = NULL;
  }

  snprintf(cat_status, sizeof(cat_status), "CAT disconnected");
  pthread_mutex_unlock(&cat_mutex);
#else
  cat_set_status("CAT unavailable (built without Hamlib)");
#endif
}

int cat_is_connected(void) {
#ifdef HAVE_HAMLIB
  pthread_mutex_lock(&cat_mutex);
  const int connected = active_rig ? 1 : 0;
  pthread_mutex_unlock(&cat_mutex);
  return connected;
#else
  return 0;
#endif
}

int cat_get_frequency_khz(int *out_khz) {
  if (!out_khz)
    return -1;

#ifdef HAVE_HAMLIB
  pthread_mutex_lock(&cat_mutex);

  if (!active_rig) {
    pthread_mutex_unlock(&cat_mutex);
    return -1;
  }

  freq_t freq_hz = 0;
  const int rc = rig_get_freq(active_rig, RIG_VFO_CURR, &freq_hz);
  pthread_mutex_unlock(&cat_mutex);

  if (rc != RIG_OK) {
    cat_set_status_fmt("CAT read frequency failed", rc);
    return -1;
  }

  *out_khz = (int)((freq_hz + 500.0) / 1000.0);
  return 0;
#else
  return -1;
#endif
}

int cat_set_frequency_khz(int freq_khz) {
  if (freq_khz <= 0)
    return -1;

#ifdef HAVE_HAMLIB
  pthread_mutex_lock(&cat_mutex);

  if (!active_rig) {
    pthread_mutex_unlock(&cat_mutex);
    return -1;
  }

  const freq_t freq_hz = (freq_t)freq_khz * 1000.0;
  const int rc = rig_set_freq(active_rig, RIG_VFO_CURR, freq_hz);
  pthread_mutex_unlock(&cat_mutex);

  if (rc != RIG_OK) {
    cat_set_status_fmt("CAT set frequency failed", rc);
    return -1;
  }

  return 0;
#else
  return -1;
#endif
}
