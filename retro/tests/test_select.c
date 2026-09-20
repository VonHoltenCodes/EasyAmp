/* multi-select in the list views, driven through the real UI entry points */
#include "../src/ui.h"
#include <stdio.h>
#include <string.h>
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static ea_model m;
static ea_srcitem lib[12];
static ea_ui *ui;

static void click(int row, int mods)
{
    int x = 300, y = 76 + 2 + row * 18 + 9;                 /* library list: top 76, rows of 18 */
    ui_set_mods(ui, mods); ui_mouse_down(ui, x, y); ui_mouse_up(ui, x, y); ui_set_mods(ui, 0);
}

static const char *marks(void)
{
    static char s[16];
    int i;
    for (i = 0; i < 12; i++) s[i] = lib[i].marked ? '#' : '.';
    s[12] = 0;
    return s;
}

int main(void)
{
    int i;
    ea_model_init(&m);
    m.page = EA_PAGE_SOURCES; m.naccts = 1; m.accts[0].state = EA_SRC_OK;
    for (i = 0; i < 12; i++) sprintf(lib[i].name, "row %d", i);
    m.src_items = lib; m.src_nitems = 12; m.src_sel = 0;
    ui = ui_create(&m, 0, 0);
    { ea_rect d[8]; ui_render(ui, d, 8); }

    click(2, 0);                 CHECK(!strcmp(marks(), "..#.........") && m.src_sel == 2, "plain click: %s sel=%d", marks(), m.src_sel);
    click(5, UI_MOD_SHIFT);      CHECK(!strcmp(marks(), "..####......"), "shift extends from the anchor: %s", marks());
    click(9, UI_MOD_CTRL);       CHECK(!strcmp(marks(), "..####...#.."), "ctrl adds a row: %s", marks());
    click(3, UI_MOD_CTRL);       CHECK(!strcmp(marks(), "..#.##...#.."), "ctrl removes a row: %s", marks());
    click(11, UI_MOD_SHIFT);     CHECK(!strcmp(marks(), "...#########"), "shift after ctrl ranges from the ctrl row: %s", marks());
    click(1, UI_MOD_SHIFT);      CHECK(!strcmp(marks(), ".###........"), "shift the other way, same anchor: %s", marks());
    click(7, 0);                 CHECK(!strcmp(marks(), ".......#...."), "plain click collapses to one: %s", marks());

    /* with nothing marked, the cursor row stands in; ctrl-clicking ANOTHER row must keep it */
    for (i = 0; i < 12; i++) lib[i].marked = 0;
    m.src_sel = 4;
    click(8, UI_MOD_CTRL);       CHECK(!strcmp(marks(), "....#...#..."), "ctrl from an unmarked cursor keeps both: %s", marks());

    ui_set_mods(ui, UI_MOD_CTRL); ui_key(ui, UI_KEY_SELECT_ALL); ui_set_mods(ui, 0);
    CHECK(!strcmp(marks(), "############"), "ctrl+A: %s", marks());
    click(0, 0);
    ui_set_mods(ui, UI_MOD_SHIFT); ui_key(ui, UI_KEY_DOWN); ui_key(ui, UI_KEY_DOWN); ui_set_mods(ui, 0);
    CHECK(!strcmp(marks(), "###.........") && m.src_sel == 2, "shift+down x2: %s sel=%d", marks(), m.src_sel);
    ui_key(ui, UI_KEY_DOWN);     CHECK(!strcmp(marks(), "...#........"), "plain down collapses: %s", marks());

    click(15, 0);                CHECK(!strcmp(marks(), "............"), "click below the last row clears: %s", marks());
    ui_destroy(ui);
    printf(fails ? "%d FAILED\n" : "all selection checks passed\n", fails);
    return fails != 0;
}
