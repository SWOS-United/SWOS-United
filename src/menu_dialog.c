/*
 * menu_dialog.c -- a dialog drawn by the game (include/menu_dialog.h): the JSON-only menu
 * openswos/menuDialog (docs/MENUS.md section 10) -- a header, four button entries -- filled in
 * by its onInit (the title, the buttons used, where they stand, their neighbours) and the text
 * drawn by its onDraw, wrapped to the screen, under the header.
 *
 * The menu font has capitals, digits and . , : - ! only: the text is shown in capitals, its
 * brackets as dashes and its quotes and apostrophes left out, so a caller can write it as it
 * would for the OS's message box. SDL builds.
 */
#include "menu_dialog.h"

#ifdef SWOS_SDL_BUILD

#include "menu_load_json.h"
#include "menu_symbols.h"
#include "menu_system.h"
#include "text_render.h"        /* GetTextWidth, DrawMenuText(Centered) */
#include "game_types.h"         /* CharsTableHeader */
#include "mod_api.h"            /* ModApi_MenuText / Visible / Nav / Select / SetMenuEntryRect */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIALOG_MENU "openswos/menuDialog"

enum { D_HEADER, D_BUTTON0 };   /* menuDialog.json's entries: the header, then the four buttons */
enum {
    kTextY = 22,                /* the text's first line, under the header */
    kTextWidth = 300,           /* px a line may take */
    kLine = 9,                  /* px from one line to the next */
    kButtonBottom = 210,        /* the last button's y; the ones before it stand above it */
    kButtonStep = 19,
};

extern void *_charTables[];
extern void  SetExitMenuFlag(void);

static struct {
    char        title[48];
    char        text[1024];
    const char *button[MENU_DIALOG_MAX_BUTTONS];
    int32_t     count, def, chosen;
} D;

/* MenuDialog_ShowText's reader: the text in the menu font's characters, its wrapped lines, the
 * page shown. (`on` 0: the menu is MenuDialog_Show's.) */
enum { kReaderWidth = 292, kReaderMaxLines = 4096, kReaderGap = 4 };
static struct {
    int32_t  on, page, pages, perPage, count;
    char    *text;
    uint16_t start[kReaderMaxLines], len[kReaderMaxLines];
    char     title[32], header[64];
} R;

/* What the menu font can show: capitals; brackets as dashes; quotes left out. */
static void to_menu_font(char *dst, size_t cap, const char *src)
{
    size_t n = 0;
    for (; *src && n + 1 < cap; src++) {
        char c = *src;
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 32);
        else if (c == '(' || c == ')' || c == '[' || c == ']')
            c = '-';
        else if (c == '\'' || c == '"')
            continue;
        else if (c == '\n')
            c = '|';
        dst[n++] = c;
    }
    dst[n] = '\0';
}

static void reader_layout(void);

static void openswosDialog_OnInit(void)
{
    if (R.on) {
        reader_layout();
        return;
    }
    ModApi_MenuText(D_HEADER, D.title);
    for (int32_t i = 0; i < MENU_DIALOG_MAX_BUTTONS; i++) {
        const int32_t e = D_BUTTON0 + i, used = i < D.count;
        ModApi_MenuVisible(e, used);
        if (!used)
            continue;
        ModApi_MenuText(e, D.button[i]);
        ModApi_SetMenuEntryRect(e, -1, kButtonBottom - (D.count - 1 - i) * kButtonStep, -1, -1);
        ModApi_MenuNav(e, i > 0 ? e - 1 : -1, i + 1 < D.count ? e + 1 : -1);
    }
    ModApi_MenuSelect(D_BUTTON0 + (D.def >= 0 && D.def < D.count ? D.def : 0));
}

/* The text, wrapped ('|' a new paragraph), centred under the header. */
static void reader_draw(void);

static void openswosDialog_OnDraw(void)
{
    if (R.on) {
        reader_draw();
        return;
    }
    const MenuEntry *header = CalcMenuEntryAddress(D_HEADER);
    const int32_t cx = header->x + header->dwWidth / 2;
    int32_t y = header->y + kTextY;
    const char *p = D.text;
    char line[128];
    while (*p) {
        int32_t n = 0, lastSpace = -1;
        if (*p == '|') {                /* an empty paragraph: half a line more */
            y += kLine / 2 + 1;
            p++;
            continue;
        }
        while (p[n] && p[n] != '|' && n < (int32_t)sizeof line - 1) {
            memcpy(line, p, (size_t)n + 1);
            line[n + 1] = '\0';
            if (GetTextWidth((uint8_t *)line, (uint8_t *)_charTables[0]) > kTextWidth)
                break;
            if (p[n] == ' ')
                lastSpace = n;
            n++;
        }
        if (p[n] && p[n] != '|' && lastSpace > 0)
            n = lastSpace;              /* (break at the last space that fits) */
        memcpy(line, p, (size_t)n);
        line[n] = '\0';
        DrawMenuTextCentered((uint16_t)cx, (uint16_t)y, 2, (uint8_t *)line, (uint8_t *)_charTables[0]);
        y += kLine;
        p += n;
        while (*p == ' ')
            p++;
        if (*p == '|') {                /* a paragraph ends: the gap, then the next */
            y += kLine / 2 + 1;
            p++;
        }
    }
}

static void reader_turn(int32_t by);

static void chose(int32_t i)
{
    if (R.on && i != 1) {               /* PREVIOUS / NEXT: the page turned, the reader stays */
        reader_turn(i == 0 ? -1 : 1);
        return;
    }
    D.chosen = i;
    SetExitMenuFlag();
}
static void openswosDialog_b0_OnSelect(void) { chose(0); }
static void openswosDialog_b1_OnSelect(void) { chose(1); }
static void openswosDialog_b2_OnSelect(void) { chose(2); }
static void openswosDialog_b3_OnSelect(void) { chose(3); }

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }
static const MenuSymbol k_handlers[] = {
    HANDLER(openswosDialog_OnInit),
    HANDLER(openswosDialog_OnDraw),
    HANDLER(openswosDialog_b0_OnSelect),
    HANDLER(openswosDialog_b1_OnSelect),
    HANDLER(openswosDialog_b2_OnSelect),
    HANDLER(openswosDialog_b3_OnSelect),
};
#undef HANDLER

int32_t MenuDialog_Show(const char *title, const char *text, const char *const *buttons, int32_t count,
                        int32_t defaultButton)
{
    static int32_t registered;
    static char buttonText[MENU_DIALOG_MAX_BUTTONS][32];
    uint8_t *menu;
    if (!registered) {
        registered = 1;
        MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
    }
    if (count < 1)
        count = 0;
    if (count > MENU_DIALOG_MAX_BUTTONS)
        count = MENU_DIALOG_MAX_BUTTONS;
    to_menu_font(D.title, sizeof D.title, title ? title : "");
    to_menu_font(D.text, sizeof D.text, text ? text : "");
    for (int32_t i = 0; i < count; i++) {
        to_menu_font(buttonText[i], sizeof buttonText[i], buttons[i] ? buttons[i] : "");
        D.button[i] = buttonText[i];
    }
    D.count = count;
    D.def = defaultButton;
    D.chosen = -1;
    menu = MenuJson_Load(DIALOG_MENU);
    if (!menu) {
        fprintf(stderr, "[dialog] cannot show %s: \"%s\"\n", DIALOG_MENU, D.title);
        return -1;
    }
    ShowMenu(menu);
    fprintf(stderr, "[dialog] %s: %s\n", D.title, D.chosen >= 0 ? D.button[D.chosen] : "(esc)");
    return D.chosen;
}

/* ---- MenuDialog_ShowText: a long text, a page at a time ---------------------------------- */

/* Whether the small font draws `c` (a Latin-1 byte): the glyph its table gives it, else a space. */
static int32_t has_glyph(uint8_t c)
{
    const CharsTableHeader *ct = (const CharsTableHeader *)_charTables[0];
    const uint16_t *conv = (const uint16_t *)(ct + 1);
    return c > ' ' && c - ' ' < 224 && conv[c - ' '] != 0;
}

/* The text as the reader keeps it: Latin-1, '\n' between lines, tabs and missing glyphs spaces,
 * no more than one empty line in a row. */
static char *reader_text(const char *src)
{
    const size_t n = strlen(src);
    char *out = (char *)malloc(n + 1);
    size_t o = 0;
    int32_t newlines = 2;               /* (no empty lines at the top) */
    if (!out)
        return NULL;
    for (const uint8_t *p = (const uint8_t *)src; *p && o < 60000; p++) {   /* (R.start: 16 bits) */
        uint8_t c = *p;
        if (c == '\r')
            continue;
        if (c >= 0xC0 && (p[1] & 0xC0) == 0x80) {      /* UTF-8: Latin-1 when it is in it */
            uint32_t cp = c & 0x1F;
            int32_t more = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
            if (more > 1)
                cp = c & (more == 2 ? 0x0F : 0x07);
            for (; more > 0 && (p[1] & 0xC0) == 0x80; more--)
                cp = (cp << 6) | (*++p & 0x3F);
            c = cp >= 0xA0 && cp <= 0xFF ? (uint8_t)cp : cp == 0x2018 || cp == 0x2019 ? '\'' :
                cp == 0x201C || cp == 0x201D ? '"' : cp == 0x2013 || cp == 0x2014 ? '-' : ' ';
        }
        if (c == '\n') {
            if (++newlines > 2)
                continue;
        } else {
            if (c == '\t' || !has_glyph(c))
                c = ' ';
            newlines = 0;
        }
        out[o++] = (char)c;
    }
    while (o > 0 && (out[o - 1] == '\n' || out[o - 1] == ' '))
        o--;
    out[o] = '\0';
    return out;
}

static int32_t width_of(const char *p, int32_t n)
{
    char line[160];
    if (n > (int32_t)sizeof line - 1)
        n = (int32_t)sizeof line - 1;
    memcpy(line, p, (size_t)n);
    line[n] = '\0';
    return GetTextWidth((uint8_t *)line, (uint8_t *)_charTables[0]);
}

/* The text's lines, wrapped to kReaderWidth: at the last space that fits, else where it fills. */
static void reader_wrap(void)
{
    const char *t = R.text;
    int32_t i = 0;
    R.count = 0;
    while (t[i] && R.count < kReaderMaxLines) {
        int32_t n = 0, lastSpace = -1;
        while (t[i + n] && t[i + n] != '\n' && n < 150 && width_of(t + i, n + 1) <= kReaderWidth) {
            if (t[i + n] == ' ')
                lastSpace = n;
            n++;
        }
        if (t[i + n] && t[i + n] != '\n' && (t[i] == '-' || t[i] == '=' || t[i] == '*' || t[i] == '_' ||
                                              t[i] == '~' || t[i] == '#')) {
            int32_t k = 0;              /* a rule (----- -----, a symbol and spaces): one line */
            while (t[i + k] && t[i + k] != '\n' && (t[i + k] == t[i] || t[i + k] == ' '))
                k++;
            if (t[i + k] == '\0' || t[i + k] == '\n') {
                R.start[R.count] = (uint16_t)i;
                R.len[R.count] = (uint16_t)n;
                R.count++;
                i += k + (t[i + k] == '\n');
                continue;
            }
        }
        if (t[i + n] && t[i + n] != '\n' && lastSpace > 0)
            n = lastSpace;              /* (else at the last space that fits) */
        if (n == 0 && t[i + n] && t[i + n] != '\n')
            n = 1;                      /* (a glyph wider than the line: shown alone) */
        R.start[R.count] = (uint16_t)i;
        R.len[R.count] = (uint16_t)n;
        R.count++;
        i += n;
        if (t[i] == '\n')
            i++;
        else
            while (t[i] == ' ')
                i++;
    }
}

/* PREVIOUS (button 0) / NEXT (2): green while they turn a page, grey (and skipped) at the first /
 * last; EXIT (1, between them) the menus' brown, as every screen's EXIT. */
static void reader_buttons(void)
{
    enum { kGreen = 14, kGrey = 9, kExit = 12 };
    const int32_t off[3] = { R.page == 0, 0, R.page >= R.pages - 1 };
    for (int32_t i = 0; i < 3; i += 2) {
        ModApi_MenuDisabled(D_BUTTON0 + i, off[i]);
        ModApi_MenuColour(D_BUTTON0 + i, off[i] ? kGrey : kGreen);
    }
    ModApi_MenuColour(D_BUTTON0 + 1, kExit);
}

static void reader_header(void)
{
    if (R.pages > 1)
        snprintf(R.header, sizeof R.header, "%s %d/%d", R.title, (int)R.page + 1, (int)R.pages);
    else
        snprintf(R.header, sizeof R.header, "%s", R.title);
    ModApi_MenuText(D_HEADER, R.header);
}

/* The three buttons in a row at the bottom -- PREVIOUS, EXIT in the centre, NEXT -- the page
 * count from the room above them. */
static void reader_layout(void)
{
    static const char *const kButtons[3] = { "PREVIOUS", "EXIT", "NEXT" };
    const MenuEntry *header = CalcMenuEntryAddress(D_HEADER);
    const int32_t cx = header->x + header->dwWidth / 2, w = (kReaderWidth - 2 * kReaderGap) / 3;
    for (int32_t i = 0; i < MENU_DIALOG_MAX_BUTTONS; i++) {
        const int32_t e = D_BUTTON0 + i;
        ModApi_MenuVisible(e, i < 3);
        if (i >= 3)
            continue;
        ModApi_MenuText(e, kButtons[i]);
        ModApi_SetMenuEntryRect(e, -1, kButtonBottom, -1, -1);
    }
    for (int32_t i = 0; i < 3; i++) {
        ModApi_SetMenuEntryRect(D_BUTTON0 + i, cx - kReaderWidth / 2 + i * (w + kReaderGap), -1, w, -1);
        ModApi_MenuNav4(D_BUTTON0 + i, i > 0 ? D_BUTTON0 + i - 1 : -1, i < 2 ? D_BUTTON0 + i + 1 : -1, -1, -1);
    }
    reader_wrap();
    {
        const MenuEntry *button = CalcMenuEntryAddress(D_BUTTON0);
        R.perPage = (button->y - 4 - (header->y + kTextY)) / kLine;
        if (R.perPage < 1)
            R.perPage = 1;
    }
    R.pages = R.count ? (R.count + R.perPage - 1) / R.perPage : 1;
    R.page = 0;
    reader_buttons();
    reader_header();
    ModApi_MenuSelect(D_BUTTON0 + (R.pages > 1 ? 2 : 1));
}

static void reader_turn(int32_t by)
{
    const int32_t page = R.page + by;
    if (page < 0 || page >= R.pages)
        return;
    R.page = page;
    reader_buttons();
    reader_header();
    ModApi_MenuRedraw(D_HEADER);
    ModApi_MenuRedraw(D_BUTTON0);
    ModApi_MenuRedraw(D_BUTTON0 + 2);
    if (R.page == R.pages - 1)          /* (the last page: the cursor off a NEXT that does nothing) */
        ModApi_MenuSelect(D_BUTTON0 + 1);
    else if (R.page == 0)               /* (the first: off PREVIOUS) */
        ModApi_MenuSelect(D_BUTTON0 + 2);
}

/* The page's lines, left-aligned under the header. */
static void reader_draw(void)
{
    extern volatile uint16_t _currentKey;
    if (_currentKey == 0x1B) {          /* ESC: as EXIT (the menus' key, menu_system.c GetKey) */
        _currentKey = 0;
        D.chosen = -1;
        SetExitMenuFlag();
    }
    const MenuEntry *header = CalcMenuEntryAddress(D_HEADER);
    const int32_t x = header->x + header->dwWidth / 2 - kReaderWidth / 2;
    int32_t y = header->y + kTextY;
    char line[160];
    for (int32_t i = R.page * R.perPage; i < R.count && i < (R.page + 1) * R.perPage; i++) {
        const int32_t n = R.len[i] < sizeof line ? R.len[i] : (int32_t)sizeof line - 1;
        memcpy(line, R.text + R.start[i], (size_t)n);
        line[n] = '\0';
        if (n)
            DrawMenuText((uint16_t)x, (uint16_t)y, 2, (uint8_t *)line, (uint8_t *)_charTables[0]);
        y += kLine;
    }
}

void MenuDialog_ShowText(const char *title, const char *text)
{
    static int32_t registered;
    uint8_t *menu;
    if (!registered) {
        registered = 1;
        MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
    }
    R.text = reader_text(text ? text : "");
    if (!R.text)
        return;
    to_menu_font(R.title, sizeof R.title, title ? title : "");
    D.count = 3;
    D.chosen = -1;
    menu = MenuJson_Load(DIALOG_MENU);
    if (menu) {
        R.on = 1;
        ShowMenu(menu);
        R.on = 0;
    } else {
        fprintf(stderr, "[dialog] cannot show %s: \"%s\"\n", DIALOG_MENU, R.title);
    }
    free(R.text);
    R.text = NULL;
}

#endif /* SWOS_SDL_BUILD */
