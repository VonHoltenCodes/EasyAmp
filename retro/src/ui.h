/* ui - EasyAmp retro's whole interface: layout, chrome, widgets, input.
 *
 * Platform-neutral. The shell owns a window and a pixel buffer; it forwards
 * mouse/keyboard/timer events here and blits the rectangles ui_render()
 * reports. The static chrome of a page is painted once into a background
 * layer; a widget that changes restores its patch of background and redraws
 * itself, so a 400 MHz machine only ever touches the pixels that moved.
 */
#ifndef EA_UI_H
#define EA_UI_H

#include "app.h"
#include "gfx.h"

#define EA_WIN_W 730
#define EA_WIN_H 578

/* ui_model_changed() flags */
#define UI_CH_TIME      0x01
#define UI_CH_TRANSPORT 0x02
#define UI_CH_TITLE     0x04
#define UI_CH_PLAYLIST  0x08
#define UI_CH_EQ        0x10
#define UI_CH_VIZ       0x20
#define UI_CH_FOOTER    0x40
#define UI_CH_SOURCES   0x80
#define UI_CH_ALL       0xff

/* key codes the shell translates to */
#define UI_MOD_SHIFT 1
#define UI_MOD_CTRL  2

enum { UI_KEY_UP = 1, UI_KEY_DOWN, UI_KEY_PGUP, UI_KEY_PGDN, UI_KEY_HOME, UI_KEY_END,
       UI_KEY_ENTER, UI_KEY_DELETE, UI_KEY_ESC, UI_KEY_SPACE, UI_KEY_SELECT_ALL };

typedef struct ea_ui ea_ui;

ea_ui      *ui_create(ea_model *m, const ea_actions *a, ea_px *pixels);
void        ui_destroy(ea_ui *ui);
ea_surface *ui_surface(ea_ui *ui);

void ui_set_page(ea_ui *ui, int page);
void ui_list_reset(ea_ui *ui);                        /* library browser entered a new level */
void ui_model_changed(ea_ui *ui, int what);
void ui_tick(ea_ui *ui, int elapsed_ms);             /* marquee, peak decay */
int  ui_render(ea_ui *ui, ea_rect *dirty, int max);  /* returns rects written */

/* Shift / Ctrl state for the NEXT mouse or key event: list multi-select */
void ui_set_mods(ea_ui *ui, int mods);
void ui_mouse_move(ea_ui *ui, int x, int y);
void ui_mouse_down(ea_ui *ui, int x, int y);
void ui_mouse_up(ea_ui *ui, int x, int y);
void ui_mouse_dbl(ea_ui *ui, int x, int y);
void ui_mouse_leave(ea_ui *ui);
void ui_wheel(ea_ui *ui, int x, int y, int notches);
void ui_key(ea_ui *ui, int key);
void ui_char(ea_ui *ui, int ch);                      /* typed text, for the sign-in form */
int  ui_is_caption(ea_ui *ui, int x, int y);         /* draggable title area */
int  ui_wants_capture(ea_ui *ui);

#endif
