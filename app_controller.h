#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
  APP_KEY_NONE = -1,
  APP_KEY_RESIZE = -2,
  APP_KEY_F1 = -3,
  APP_KEY_F2 = -4,
  APP_KEY_F3 = -5,
  APP_KEY_F4 = -6,
  APP_KEY_F5 = -7,
  APP_KEY_F6 = -8,
  APP_KEY_F7 = -9,
  APP_KEY_F10 = -11,
  APP_KEY_UP = -12,
  APP_KEY_DOWN = -13,
  APP_KEY_PAGE_UP = -14,
  APP_KEY_PAGE_DOWN = -15,
  APP_KEY_BACKSPACE = -16,
  APP_KEY_ENTER = -17,
  APP_KEY_TAB = -18,
  APP_KEY_ESC = -19,
  APP_KEY_SPACE = ' '
};

typedef enum {
  APP_CTRL_EVENT_NONE = 0,
  APP_CTRL_EVENT_REQUEST_CTY_UPDATE,
  APP_CTRL_EVENT_EXIT,
} AppControllerEvent;

typedef struct {
  const char *input;
  const char *input_call;
  const char *input_rst;
  const char *input_comments;
  int active_input_field;
  const char *status;
  const char *dxcc;
  const char *info;
  bool cluster_view;
  int cluster_scroll;
} AppRenderState;

/*
 * Initialize shared application state and start background services.
 *
 * @return 0 on success, or -1 if initialization fails.
 */
int app_controller_init(void);

/*
 * Shut down shared application state and stop background services.
 *
 * @return Nothing.
 */
void app_controller_shutdown(void);

/*
 * Copy the current render state into out for UI consumers.
 *
 * @param out Destination structure to fill. Must point to a valid
 *            AppRenderState instance.
 * @return Nothing.
 */
void app_controller_get_render_state(AppRenderState *out);

/*
 * Handle a translated key code and update shared controller state.
 *
 * @param key One of the APP_KEY_* values.
 * @return The controller event the UI should react to, or
 *         APP_CTRL_EVENT_NONE if no special action is required.
 */
AppControllerEvent app_controller_handle_key(int key);

/*
 * Download and reload the latest CTY database.
 *
 * @return Nothing.
 */
void app_controller_perform_cty_update(void);

/*
 * Get current operating frequency in kHz used for split-entry QSOs.
 *
 * @return Current frequency in kHz.
 */
int app_controller_get_active_frequency_khz(void);

#ifdef __cplusplus
}
#endif

#endif