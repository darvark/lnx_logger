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
  APP_KEY_F10 = -9,
  APP_KEY_UP = -10,
  APP_KEY_DOWN = -11,
  APP_KEY_PAGE_UP = -12,
  APP_KEY_PAGE_DOWN = -13,
  APP_KEY_BACKSPACE = -14,
  APP_KEY_ENTER = -15,
  APP_KEY_TAB = -16,
  APP_KEY_ESC = -17,
  APP_KEY_SPACE = ' '
};

typedef enum {
  APP_CTRL_EVENT_NONE = 0,
  APP_CTRL_EVENT_REQUEST_CTY_UPDATE,
  APP_CTRL_EVENT_EXIT,
} AppControllerEvent;

typedef struct {
  const char *input;
  const char *status;
  const char *dxcc;
  const char *info;
  bool cluster_view;
  int cluster_scroll;
} AppRenderState;

int app_controller_init(void);
void app_controller_shutdown(void);
void app_controller_get_render_state(AppRenderState *out);
AppControllerEvent app_controller_handle_key(int key);
void app_controller_perform_cty_update(void);

#ifdef __cplusplus
}
#endif

#endif