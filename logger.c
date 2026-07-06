#include "app_controller.h"
#include "ui.h"

static int translate_key(int ch) {
  if (ch == ERR)
    return APP_KEY_NONE;

  if (ch == KEY_RESIZE)
    return APP_KEY_RESIZE;
  if (ch == KEY_F(1))
    return APP_KEY_F1;
  if (ch == KEY_F(2))
    return APP_KEY_F2;
  if (ch == KEY_F(3))
    return APP_KEY_F3;
  if (ch == KEY_F(4))
    return APP_KEY_F4;
  if (ch == KEY_F(5))
    return APP_KEY_F5;
  if (ch == KEY_F(6))
    return APP_KEY_F6;
  if (ch == KEY_F(7))
    return APP_KEY_F7;
  if (ch == KEY_F(10))
    return APP_KEY_F10;
  if (ch == KEY_UP)
    return APP_KEY_UP;
  if (ch == KEY_DOWN)
    return APP_KEY_DOWN;
  if (ch == KEY_PPAGE)
    return APP_KEY_PAGE_UP;
  if (ch == KEY_NPAGE)
    return APP_KEY_PAGE_DOWN;
  if (ch == KEY_BACKSPACE || ch == 127)
    return APP_KEY_BACKSPACE;
  if (ch == '\n' || ch == '\r')
    return APP_KEY_ENTER;
  if (ch == '\t')
    return APP_KEY_TAB;
  if (ch == 27)
    return APP_KEY_ESC;

  return ch;
}

int main(void) {
  AppRenderState state;

  ui_init();

  app_controller_init();

  while (1) {
    app_controller_get_render_state(&state);

    if (state.cluster_view)
      draw_cluster_fullscreen(state.cluster_scroll);
    else
      draw_all(state.input, state.status, state.dxcc, state.info);

    int ch = wgetch(w_input);
    int key = translate_key(ch);

    if (key == APP_KEY_RESIZE) {
      ui_resize();
      continue;
    }

    AppControllerEvent event = app_controller_handle_key(key);

    if (event == APP_CTRL_EVENT_EXIT)
      break;

    if (event == APP_CTRL_EVENT_REQUEST_CTY_UPDATE) {
      app_controller_get_render_state(&state);
      if (state.cluster_view)
        draw_cluster_fullscreen(state.cluster_scroll);
      else
        draw_all(state.input, state.status, state.dxcc, state.info);
      app_controller_perform_cty_update();
    }
  }

  app_controller_shutdown();

  ui_shutdown();

  return 0;
}
