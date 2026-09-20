/* EasyAmp retro - the Win32 shell. One ANSI executable for Windows 98 SE
 * through XP: a borderless self-drawn window, the playlist, file dialogs and
 * the glue between the UI (ui.c) and the audio engine (engine_win32.c). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine.h"
#include "ui.h"

#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL 0x020A
#endif
#define OFN_SIZE_V400 76                 /* Windows 98 rejects the larger NT5 struct */
#define TIMER_ID 1
#define FRAME_MS 33

typedef struct { char path[MAX_PATH]; } item;

/* how the finished picture reaches the screen, by desktop colour depth */
enum { PRESENT_DIRECT,      /* 24/32-bit: as drawn */
       PRESENT_HICOLOR,     /* 15/16-bit: our own ordered dither (GDI would truncate) */
       PRESENT_PAL256,      /* 256 colours: our palette, tuned to the skin, + dither */
       PRESENT_VGA16 };     /* 16 colours: dither into the fixed VGA palette */

static HWND       g_wnd;
static HDC        g_memdc;
static HBITMAP    g_dib, g_olddib;
static HPALETTE   g_pal;
static void      *g_dibbits;
static int        g_present, g_dib_stride, g_green_bits, g_force_depth;
static ea_model   g_m;
static ea_ui     *g_ui;
static ea_engine *g_eng;
static item      *g_items;
static int        g_cap;
static DWORD      g_last_tick;
static char       g_shot[MAX_PATH];
static DWORD      g_shot_at;

/* ---- playlist ------------------------------------------------------------------ */

static void id3_field(const unsigned char *p, int n, char *out, int cap)
{
    int i, o = 0;
    while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == 0)) n--;
    for (i = 0; i < n && o < cap - 1; i++) if (p[i] >= 32) out[o++] = (char)(p[i] < 127 ? p[i] : '?');
    out[o] = 0;
}

/* "Artist - Title" from an ID3v2 (TPE1/TIT2) or ID3v1 tag, else the file name */
static void read_title(const char *path, char *out, int cap)
{
    char artist[64] = "", title[96] = "";
    const char *base = strrchr(path, '\\'), *dot;
    FILE *f = fopen(path, "rb");
    if (f) {
        unsigned char h[10];
        if (fread(h, 1, 10, f) == 10 && !memcmp(h, "ID3", 3) && h[3] >= 3) {
            long size = ((long)h[6] & 127) << 21 | ((long)h[7] & 127) << 14 | ((long)h[8] & 127) << 7 | ((long)h[9] & 127), pos = 0;
            while (pos + 10 < size && pos < 65536 && !(artist[0] && title[0])) {
                unsigned char fh[10], buf[128];
                long fs;
                int take, k, o;
                char *dst;
                if (fread(fh, 1, 10, f) != 10 || fh[0] == 0) break;
                fs = h[3] == 4 ? (((long)fh[4] & 127) << 21 | ((long)fh[5] & 127) << 14 | ((long)fh[6] & 127) << 7 | ((long)fh[7] & 127))
                               : ((long)fh[4] << 24 | (long)fh[5] << 16 | (long)fh[6] << 8 | (long)fh[7]);
                if (fs <= 0 || fs > size) break;
                dst = !memcmp(fh, "TPE1", 4) ? artist : !memcmp(fh, "TIT2", 4) ? title : 0;
                take = fs > (long)sizeof buf ? (int)sizeof buf : (int)fs;
                if (dst && fread(buf, 1, (size_t)take, f) == (size_t)take) {
                    int capd = dst == artist ? (int)sizeof artist : (int)sizeof title, wide = buf[0] == 1 || buf[0] == 2;
                    for (k = 1, o = 0; k < take && o < capd - 1; k += wide ? 2 : 1) {
                        unsigned char c = buf[k], hi = wide && k + 1 < take ? buf[k + 1] : 0;
                        if (wide && buf[0] == 2) { unsigned char t = c; c = hi; hi = t; }
                        if (wide && ((c == 0xff && hi == 0xfe) || (c == 0xfe && hi == 0xff))) continue;   /* BOM */
                        if (c >= 32 && !hi) dst[o++] = (char)(c < 127 ? c : '?');
                        else if (hi) dst[o++] = '?';
                    }
                    dst[o] = 0;
                    fseek(f, fs - take, SEEK_CUR);
                } else fseek(f, dst ? fs - take : fs, SEEK_CUR);
                pos += 10 + fs;
            }
        }
        if (!title[0] && !fseek(f, -128, SEEK_END)) {
            unsigned char t[128];
            if (fread(t, 1, 128, f) == 128 && !memcmp(t, "TAG", 3)) { id3_field(t + 3, 30, title, sizeof title); id3_field(t + 33, 30, artist, sizeof artist); }
        }
        fclose(f);
    }
    if (title[0]) {
        if (artist[0]) _snprintf(out, (size_t)cap, "%s - %s", artist, title); else _snprintf(out, (size_t)cap, "%s", title);
        out[cap - 1] = 0;
        return;
    }
    base = base ? base + 1 : path;
    strncpy(out, base, (size_t)cap - 1); out[cap - 1] = 0;
    dot = strrchr(out, '.');
    if (dot) *(char *)dot = 0;
}

static int playable(const char *path)
{
    const char *dot = strrchr(path, '.');
    return dot && (!lstrcmpiA(dot, ".mp3") || !lstrcmpiA(dot, ".mp2") || !lstrcmpiA(dot, ".wav"));
}

static void pl_add(const char *path)
{
    if (!playable(path)) return;
    if (g_m.ntracks == g_cap) {
        int ncap = g_cap ? g_cap * 2 : 64;
        item *ni = (item *)realloc(g_items, sizeof(item) * (size_t)ncap);
        ea_track *nt = (ea_track *)realloc(g_m.tracks, sizeof(ea_track) * (size_t)ncap);
        if (ni) g_items = ni;
        if (nt) g_m.tracks = nt;
        if (!ni || !nt) return;
        g_cap = ncap;
    }
    strncpy(g_items[g_m.ntracks].path, path, MAX_PATH - 1); g_items[g_m.ntracks].path[MAX_PATH - 1] = 0;
    memset(&g_m.tracks[g_m.ntracks], 0, sizeof(ea_track));
    read_title(path, g_m.tracks[g_m.ntracks].title, (int)sizeof g_m.tracks[0].title);
    g_m.ntracks++;
}

static void pl_clear(void) { g_m.ntracks = 0; g_m.sel = g_m.cur = -1; }

static void pl_remove(int i)
{
    if (i < 0 || i >= g_m.ntracks) return;
    memmove(&g_items[i], &g_items[i + 1], sizeof(item) * (size_t)(g_m.ntracks - i - 1));
    memmove(&g_m.tracks[i], &g_m.tracks[i + 1], sizeof(ea_track) * (size_t)(g_m.ntracks - i - 1));
    g_m.ntracks--;
    if (g_m.cur == i) g_m.cur = -1; else if (g_m.cur > i) g_m.cur--;
    if (g_m.sel >= g_m.ntracks) g_m.sel = g_m.ntracks - 1;
}

static void play_index(void *ctx, int i)
{
    (void)ctx;
    if (i < 0 || i >= g_m.ntracks) return;
    g_m.cur = g_m.sel = i;
    strncpy(g_m.title, g_m.tracks[i].title, sizeof g_m.title - 1);
    eng_open(g_eng, g_items[i].path);
    ui_model_changed(g_ui, UI_CH_TITLE | UI_CH_PLAYLIST | UI_CH_TRANSPORT);
}

static void m3u_load(const char *path)
{
    char line[1024], dir[MAX_PATH], full[MAX_PATH], *slash;
    FILE *f = fopen(path, "r");
    if (!f) return;
    strncpy(dir, path, MAX_PATH - 1); dir[MAX_PATH - 1] = 0;
    slash = strrchr(dir, '\\');
    if (slash) slash[1] = 0; else dir[0] = 0;
    while (fgets(line, sizeof line, f)) {
        size_t n = strlen(line);
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
        if (!n || line[0] == '#') continue;
        if (line[1] == ':' || line[0] == '\\') strncpy(full, line, MAX_PATH - 1);       /* absolute */
        else _snprintf(full, MAX_PATH, "%s%s", dir, line);
        full[MAX_PATH - 1] = 0;
        pl_add(full);
    }
    fclose(f);
}

static void m3u_save(const char *path)
{
    FILE *f = fopen(path, "w");
    int i;
    if (!f) return;
    fprintf(f, "#EXTM3U\n");
    for (i = 0; i < g_m.ntracks; i++) fprintf(f, "#EXTINF:-1,%s\n%s\n", g_m.tracks[i].title, g_items[i].path);
    fclose(f);
}

/* ---- dialogs ----------------------------------------------------------------------- */

static int ask_file(int save, const char *filter, const char *defext, char *buf, int cap, int multi)
{
    OPENFILENAMEA o;
    memset(&o, 0, sizeof o);
    o.lStructSize = OFN_SIZE_V400;
    o.hwndOwner = g_wnd; o.lpstrFilter = filter; o.lpstrFile = buf; o.nMaxFile = (DWORD)cap; o.lpstrDefExt = defext;
    o.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST) | (multi ? OFN_ALLOWMULTISELECT : 0);
    buf[0] = 0;
    return save ? GetSaveFileNameA(&o) : GetOpenFileNameA(&o);
}

static void add_files_dialog(int replace_and_play)
{
    static char buf[32768];
    int first = replace_and_play ? 0 : g_m.ntracks;
    if (!ask_file(0, "Music (*.mp3;*.mp2;*.wav)\0*.mp3;*.mp2;*.wav\0All files\0*.*\0", 0, buf, (int)sizeof buf, 1)) return;
    if (replace_and_play) { eng_stop(g_eng); pl_clear(); }
    if (buf[strlen(buf) + 1] == 0) pl_add(buf);                     /* a single file */
    else {                                                        /* dir\0name\0name\0\0 */
        const char *dir = buf, *p = buf + strlen(buf) + 1;
        char full[MAX_PATH];
        for (; *p; p += strlen(p) + 1) { _snprintf(full, MAX_PATH, "%s\\%s", dir, p); full[MAX_PATH - 1] = 0; pl_add(full); }
    }
    ui_model_changed(g_ui, UI_CH_PLAYLIST);
    if (replace_and_play && g_m.ntracks > first) play_index(0, first);
}

/* ---- actions from the UI ------------------------------------------------------------ */

static void on_command(void *ctx, int cmd)
{
    char path[MAX_PATH];
    (void)ctx;
    switch (cmd) {
    case EA_CMD_EJECT:     add_files_dialog(1); break;
    case EA_CMD_PL_ADD:    add_files_dialog(0); break;
    case EA_CMD_PLAYPAUSE:
        if (g_m.state == EA_STOPPED) play_index(0, g_m.sel >= 0 ? g_m.sel : (g_m.cur >= 0 ? g_m.cur : 0));
        else eng_pause(g_eng, g_m.state == EA_PLAYING);
        break;
    case EA_CMD_STOP:      eng_stop(g_eng); break;
    case EA_CMD_PREV:      if (g_m.ntracks) play_index(0, g_m.cur > 0 ? g_m.cur - 1 : 0); break;
    case EA_CMD_NEXT:      if (g_m.cur + 1 < g_m.ntracks) play_index(0, g_m.cur + 1); break;
    case EA_CMD_PL_REMOVE: if (g_m.sel == g_m.cur && g_m.cur >= 0) eng_stop(g_eng);
                           pl_remove(g_m.sel); ui_model_changed(g_ui, UI_CH_PLAYLIST); break;
    case EA_CMD_PL_CLEAR:  eng_stop(g_eng); pl_clear(); g_m.title[0] = 0; ui_model_changed(g_ui, UI_CH_PLAYLIST | UI_CH_TITLE); break;
    case EA_CMD_PL_LOAD:
        if (ask_file(0, "Playlist (*.m3u)\0*.m3u;*.m3u8\0", 0, path, MAX_PATH, 0)) { eng_stop(g_eng); pl_clear(); m3u_load(path); ui_model_changed(g_ui, UI_CH_PLAYLIST); }
        break;
    case EA_CMD_PL_SAVE:
        if (g_m.ntracks && ask_file(1, "Playlist (*.m3u)\0*.m3u\0", "m3u", path, MAX_PATH, 0)) m3u_save(path);
        break;
    case EA_CMD_WIN_MINIMIZE: ShowWindow(g_wnd, SW_MINIMIZE); break;
    case EA_CMD_WIN_CLOSE:    PostMessageA(g_wnd, WM_CLOSE, 0, 0); break;
    case EA_CMD_OPEN_UPDATE:  ShellExecuteA(g_wnd, "open", "https://www.easyampstereo.com/download.html", 0, 0, SW_SHOWNORMAL); break;
    }
}

static void on_seek(void *ctx, float f) { (void)ctx; if (g_m.state != EA_STOPPED) eng_seek(g_eng, f); }
static void on_eq(void *ctx) { (void)ctx; eng_set_dsp(g_eng, &g_m); }

/* ---- painting ------------------------------------------------------------------------ */

static void presenter_free(void)
{
    if (g_memdc) { if (g_olddib) SelectObject(g_memdc, g_olddib); DeleteDC(g_memdc); }
    if (g_dib) DeleteObject(g_dib);
    if (g_pal) DeleteObject(g_pal);
    g_memdc = 0; g_dib = 0; g_olddib = 0; g_pal = 0; g_dibbits = 0;
}

/* (re)build the off-screen bitmap for the desktop's current colour depth */
static int presenter_init(void)
{
    struct { BITMAPINFOHEADER h; union { RGBQUAD rgb[256]; DWORD mask[3]; } c; } bi;
    HDC screen = GetDC(0);
    int bpp = GetDeviceCaps(screen, BITSPIXEL) * GetDeviceCaps(screen, PLANES), i, bits;
    if (g_force_depth) bpp = g_force_depth;
    presenter_free();
    memset(&bi, 0, sizeof bi);
    bi.h.biSize = sizeof bi.h; bi.h.biWidth = EA_WIN_W; bi.h.biHeight = -EA_WIN_H; bi.h.biPlanes = 1; bi.h.biCompression = BI_RGB;
    if (bpp >= 24) { g_present = PRESENT_DIRECT; bits = 32; }
    else if (bpp >= 15) {
        g_present = PRESENT_HICOLOR; bits = 16;
        g_green_bits = bpp == 15 ? 5 : 6;
        if (g_green_bits == 6) { bi.h.biCompression = BI_BITFIELDS; bi.c.mask[0] = 0xf800; bi.c.mask[1] = 0x07e0; bi.c.mask[2] = 0x001f; }
    } else {
        const unsigned char *pal = bpp >= 8 ? EA_PAL256 : EA_PAL16;
        int n = bpp >= 8 ? EA_PAL256_N : 16;
        g_present = bpp >= 8 ? PRESENT_PAL256 : PRESENT_VGA16; bits = 8;
        bi.h.biClrUsed = (DWORD)n;
        for (i = 0; i < n; i++) { bi.c.rgb[i].rgbRed = pal[i * 3]; bi.c.rgb[i].rgbGreen = pal[i * 3 + 1]; bi.c.rgb[i].rgbBlue = pal[i * 3 + 2]; }
        if (g_present == PRESENT_PAL256) {
            /* our own logical palette: Windows keeps 20 system colours, the
             * other 236 slots become the skin's while we are in front */
            struct { WORD ver, n; PALETTEENTRY e[256]; } lp;
            lp.ver = 0x300; lp.n = (WORD)n;
            for (i = 0; i < n; i++) { lp.e[i].peRed = pal[i * 3]; lp.e[i].peGreen = pal[i * 3 + 1]; lp.e[i].peBlue = pal[i * 3 + 2]; lp.e[i].peFlags = 0; }
            g_pal = CreatePalette((LOGPALETTE *)&lp);
        }
    }
    bi.h.biBitCount = (WORD)bits;
    g_dib_stride = ((EA_WIN_W * bits + 31) / 32) * 4;
    g_memdc = CreateCompatibleDC(screen);
    g_dib = CreateDIBSection(screen, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &g_dibbits, 0, 0);
    ReleaseDC(0, screen);
    if (!g_memdc || !g_dib || !g_dibbits) return 0;
    g_olddib = (HBITMAP)SelectObject(g_memdc, g_dib);
    return 1;
}

/* convert one rect of the UI's picture into the off-screen bitmap */
static void convert(int x, int y, int w, int h)
{
    ea_surface *s = ui_surface(g_ui);
    int j;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > EA_WIN_W) w = EA_WIN_W - x;
    if (y + h > EA_WIN_H) h = EA_WIN_H - y;
    if (w <= 0 || h <= 0) return;
    switch (g_present) {
    case PRESENT_DIRECT:
        for (j = 0; j < h; j++) memcpy((ea_px *)g_dibbits + (y + j) * EA_WIN_W + x, s->px + (y + j) * EA_WIN_W + x, (size_t)w * 4);
        break;
    case PRESENT_HICOLOR: gfx_dither16(s, x, y, w, h, (unsigned short *)g_dibbits, g_dib_stride, g_green_bits); break;
    case PRESENT_PAL256:  gfx_dither_indexed(s, x, y, w, h, (unsigned char *)g_dibbits, g_dib_stride, EA_LUT256, 20); break;
    case PRESENT_VGA16:   gfx_dither_indexed(s, x, y, w, h, (unsigned char *)g_dibbits, g_dib_stride, EA_LUT16, 96); break;
    }
}

static void use_palette(HDC dc)
{
    if (g_pal) { SelectPalette(dc, g_pal, FALSE); RealizePalette(dc); }
}

static void present(HDC dc)
{
    ea_rect d[48];
    int n = ui_render(g_ui, d, 48), i;
    use_palette(dc);
    for (i = 0; i < n; i++) {
        convert(d[i].x, d[i].y, d[i].w, d[i].h);
        BitBlt(dc, d[i].x, d[i].y, d[i].w, d[i].h, g_memdc, d[i].x, d[i].y, SRCCOPY);
    }
}

/* /shot: save the picture as presented - read back through GDI from the
 * off-screen bitmap, so an indexed or 16-bit mode is captured as the screen
 * would show it and the colour table itself is part of what gets checked */
static void save_shot(void)
{
    BITMAPFILEHEADER fh;
    BITMAPINFO bi;
    HDC screen = GetDC(0), dc = CreateCompatibleDC(screen);
    void *bits = 0;
    HBITMAP bm, old;
    FILE *f;
    memset(&bi, 0, sizeof bi);
    bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = EA_WIN_W; bi.bmiHeader.biHeight = EA_WIN_H;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    bm = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, 0, 0);
    ReleaseDC(0, screen);
    if (!dc || !bm) return;
    old = (HBITMAP)SelectObject(dc, bm);
    convert(0, 0, EA_WIN_W, EA_WIN_H);
    BitBlt(dc, 0, 0, EA_WIN_W, EA_WIN_H, g_memdc, 0, 0, SRCCOPY);
    GdiFlush();
    f = fopen(g_shot, "wb");
    if (f) {
        memset(&fh, 0, sizeof fh);
        fh.bfType = 0x4d42; fh.bfOffBits = sizeof fh + sizeof bi.bmiHeader; fh.bfSize = fh.bfOffBits + EA_WIN_W * EA_WIN_H * 4;
        fwrite(&fh, sizeof fh, 1, f); fwrite(&bi.bmiHeader, sizeof bi.bmiHeader, 1, f);
        fwrite(bits, 4, EA_WIN_W * EA_WIN_H, f);
        fclose(f);
    }
    SelectObject(dc, old); DeleteObject(bm); DeleteDC(dc);
}

static void frame(void)
{
    DWORD now = GetTickCount();
    int old_state = g_m.state, old_sec = g_m.pos_ms / 1000, old_kbps = g_m.kbps, ev, what = UI_CH_VIZ;
    HDC dc;
    POINT pt;
    RECT rc;
    ev = eng_poll(g_eng, &g_m);
    if (g_m.state != old_state || g_m.kbps != old_kbps) what |= UI_CH_TRANSPORT | UI_CH_TIME;
    if (g_m.pos_ms / 1000 != old_sec || g_m.state == EA_PLAYING) what |= UI_CH_TIME;
    if (ev == ENG_EV_ENDED) { if (g_m.cur + 1 < g_m.ntracks) play_index(0, g_m.cur + 1); else { g_m.cur = -1; what |= UI_CH_PLAYLIST; } }
    if (ev == ENG_EV_ERROR) { strcpy(g_m.title, "LOAD ERROR"); what |= UI_CH_TITLE; }   /* never auto-advance on errors */
    ui_model_changed(g_ui, what);
    ui_tick(g_ui, (int)(now - g_last_tick));
    g_last_tick = now;
    GetCursorPos(&pt); GetWindowRect(g_wnd, &rc);
    if (!PtInRect(&rc, pt)) ui_mouse_leave(g_ui);
    dc = GetDC(g_wnd);
    present(dc);
    ReleaseDC(g_wnd, dc);
    if (g_shot[0] && now >= g_shot_at) { save_shot(); g_shot[0] = 0; PostMessageA(g_wnd, WM_CLOSE, 0, 0); }
}

static int map_key(WPARAM vk)
{
    switch (vk) {
    case VK_UP: return UI_KEY_UP;       case VK_DOWN: return UI_KEY_DOWN;
    case VK_PRIOR: return UI_KEY_PGUP;  case VK_NEXT: return UI_KEY_PGDN;
    case VK_HOME: return UI_KEY_HOME;   case VK_END: return UI_KEY_END;
    case VK_RETURN: return UI_KEY_ENTER; case VK_DELETE: return UI_KEY_DELETE;
    case VK_ESCAPE: return UI_KEY_ESC;  case VK_SPACE: return UI_KEY_SPACE;
    }
    return 0;
}

static LRESULT CALLBACK wndproc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        int pw = ps.rcPaint.right - ps.rcPaint.left, ph = ps.rcPaint.bottom - ps.rcPaint.top;
        present(dc);
        convert(ps.rcPaint.left, ps.rcPaint.top, pw, ph);
        BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, pw, ph, g_memdc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        EndPaint(h, &ps);
        return 0; }
    /* 256-colour desktops: take the palette when we come to the front, and
     * re-map when another program has just taken it */
    case WM_QUERYNEWPALETTE:
        if (g_pal) { HDC dc = GetDC(h); use_palette(dc); ReleaseDC(h, dc); InvalidateRect(h, 0, FALSE); return TRUE; }
        return FALSE;
    case WM_PALETTECHANGED:
        if (g_pal && (HWND)wp != h) { HDC dc = GetDC(h); use_palette(dc); ReleaseDC(h, dc); InvalidateRect(h, 0, FALSE); }
        return 0;
    case WM_DISPLAYCHANGE:          /* the user changed colour depth: pick the matching path */
        presenter_init();
        InvalidateRect(h, 0, FALSE);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_TIMER: frame(); return 0;
    case WM_NCHITTEST: {
        POINT p;
        p.x = x; p.y = y;
        ScreenToClient(h, &p);
        return ui_is_caption(g_ui, p.x, p.y) ? HTCAPTION : HTCLIENT; }
    case WM_MOUSEMOVE:   ui_mouse_move(g_ui, x, y); return 0;
    case WM_LBUTTONDOWN: SetCapture(h); ui_mouse_down(g_ui, x, y); return 0;
    case WM_LBUTTONUP:   ui_mouse_up(g_ui, x, y); ReleaseCapture(); return 0;
    case WM_LBUTTONDBLCLK: ui_mouse_dbl(g_ui, x, y); return 0;
    case WM_MOUSEWHEEL: {
        POINT p;
        p.x = x; p.y = y;
        ScreenToClient(h, &p);
        ui_wheel(g_ui, p.x, p.y, (short)HIWORD(wp) / 120);
        return 0; }
    case WM_KEYDOWN: { int k = map_key(wp); if (k) ui_key(g_ui, k); return 0; }
    case WM_DROPFILES: {
        HDROP d = (HDROP)wp;
        UINT n = DragQueryFileA(d, 0xFFFFFFFF, 0, 0), i;
        char path[MAX_PATH];
        int first = g_m.ntracks;
        for (i = 0; i < n; i++) if (DragQueryFileA(d, i, path, MAX_PATH)) pl_add(path);
        DragFinish(d);
        ui_model_changed(g_ui, UI_CH_PLAYLIST);
        if (g_m.state == EA_STOPPED && g_m.ntracks > first) play_index(0, first);
        return 0; }
    case WM_DESTROY: KillTimer(h, TIMER_ID); PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(h, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    WNDCLASSA wc;
    ea_actions act;
    MSG msg;
    int i, sx, sy, page = 0, autoplay = 0;
    (void)prev; (void)cmdline;

    ea_model_init(&g_m);
    act.ctx = 0; act.command = on_command; act.seek = on_seek; act.play_index = play_index; act.eq_changed = on_eq;
    g_ui = ui_create(&g_m, &act, 0);
    g_eng = eng_create();
    if (!g_ui || !g_eng) { MessageBoxA(0, "Out of memory.", "EasyAmp", MB_ICONERROR); return 1; }

    for (i = 1; i < __argc; i++) {
        const char *a = __argv[i];
        if (!strncmp(a, "/shot:", 6)) { strncpy(g_shot, a + 6, MAX_PATH - 1); if (!g_shot_at) g_shot_at = 1500; }
        else if (!strncmp(a, "/shotms:", 8)) g_shot_at = (DWORD)atoi(a + 8);
        else if (!strncmp(a, "/page:", 6)) page = atoi(a + 6);
        else if (!strncmp(a, "/depth:", 7)) g_force_depth = atoi(a + 7);      /* 32, 16, 15, 8, 4: try a colour path on any desktop */
        else if (!strncmp(a, "/preset:", 8)) { int p = atoi(a + 8); if (p >= 0 && p < ea_preset_count()) ea_preset_apply(&g_m, p); }
        else if (!strncmp(a, "/vu", 3)) g_m.viz_vu = 1;
        else { const char *dot = strrchr(a, '.'); if (dot && !lstrcmpiA(dot, ".m3u")) m3u_load(a); else pl_add(a); autoplay = 1; }
    }
    eng_set_dsp(g_eng, &g_m);
    if (!presenter_init()) { MessageBoxA(0, "Could not create the display surface.", "EasyAmp", MB_ICONERROR); return 1; }

    memset(&wc, 0, sizeof wc);
    wc.style = CS_DBLCLKS | CS_OWNDC;
    wc.lpfnWndProc = wndproc; wc.hInstance = inst; wc.lpszClassName = "EasyAmpRetro";
    wc.hCursor = LoadCursorA(0, IDC_ARROW);
    wc.hIcon = LoadIconA(inst, MAKEINTRESOURCEA(1));
    if (!RegisterClassA(&wc)) return 1;
    sx = (GetSystemMetrics(SM_CXSCREEN) - EA_WIN_W) / 2; sy = (GetSystemMetrics(SM_CYSCREEN) - EA_WIN_H) / 2;
    g_wnd = CreateWindowExA(WS_EX_ACCEPTFILES, "EasyAmpRetro", "EasyAmp", WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX,
                            sx < 0 ? 0 : sx, sy < 0 ? 0 : sy, EA_WIN_W, EA_WIN_H, 0, 0, inst, 0);
    if (!g_wnd) return 1;
    ui_set_page(g_ui, page);
    if (autoplay && g_m.ntracks) play_index(0, 0);
    g_last_tick = GetTickCount();
    if (g_shot[0]) g_shot_at += g_last_tick;
    ShowWindow(g_wnd, show);
    UpdateWindow(g_wnd);
    SetTimer(g_wnd, TIMER_ID, FRAME_MS, 0);
    while (GetMessageA(&msg, 0, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    eng_destroy(g_eng);
    ui_destroy(g_ui);
    presenter_free();
    return 0;
}
