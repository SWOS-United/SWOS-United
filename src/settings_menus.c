/*
 * settings_menus.c -- SWOS 2020's OPTIONS screens, ported (SDL builds):
 *
 *   SETTINGS (openswos/settingsMenu)   SWOS 2020's OPTIONS buttons: CONTROLS, VIDEO, AUDIO,
 *                                      GAMEPLAY -- opened from OPTIONS' SETTINGS row
 *   a page (openswos/settingsPage)     one of those four screens, as rows of OUR settings
 *                                      (SWOS 2020's controlOptions.mnu, videoOptions.mnu,
 *                                      audioOptions.mnu, gameplayOptions in options.mnu):
 *                                      fire or right changes a value, left changes it back
 *   CONTROLLER SETUP                   SWOS 2020's joypadConfig.mnu, imported as it is
 *   (openswos/joypadConfigMenu)        (scripts/import_swos2020_menus.py): a pad's name, GUID,
 *                                      buttons, axes, power and raw stick, its FIRE and BENCH
 *                                      buttons and its four stick cut-offs with the box
 *
 * and F6, SWOS 2020's quick save from the menus (controls.cpp:2202), and the menu cursor's
 * flashing (SWOS 2020's --flashing-menu-cursor).
 *
 * Every row is a setting the game already has in SWOS.ini: it changes now where the game can
 * (the window, the volumes, the sounds, the controls), and says "from the next start" where it
 * cannot. The settings themselves are where they always were -- the controls in input_sdl.c,
 * the volumes in audio_sdl.c, the replay viewer in replay_v2.c -- this file only shows and
 * changes them and saves SWOS.ini at once, as every row of OPTIONS does. Nothing here changes
 * a match: online, each game's controls only decide what its own input words say.
 *
 * Menus JSON-only (docs/MENUS.md section 10), flagged dos_redraw so onDraw runs every frame.
 */
#ifdef SWOS_SDL_BUILD

#include "menu_dialog.h"
#include "kit_variants.h"
#include "menu_load_json.h"
#include "menu_symbols.h"
#include "menu_system.h"
#include "platform_sdl.h"
#include "swos_settings.h"
#include "dos_mode.h"
#include "dos_sound.h"
#include "dos_music.h"
#include "tweaks.h"            /* STRONGER KEEPERS: [tweaks] stronger_goalkeeper_ai */
#include "project_identity.h"  /* MENU LOOK: PROJECT_MENU_LOOK_AMIGA */
#include "pixel_filter.h"      /* DISPLAY OPTIONS (launcher options, agent B) */
#include "mod_api.h"           /* ... its mods' settings */
#include "mods.h"
#include "team_colors.h"       /* (its self-test) */
#include "match_setup.h"
#include "dos_graphics.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

#define SETTINGS_MENU_NAME   "openswos/settingsMenu"
#define SETTINGS_PAGE_NAME   "openswos/settingsPage"
#define JOYPAD_CONFIG_NAME   "openswos/joypadConfigMenu"

extern PlatformInitOptions *g_arcadeOpts;
extern const char *g_arcadeIniPath;

extern void      MenuCheckControls(void);
extern void      CallDrawMenuItem(MenuEntry *entry);
extern void      SetExitMenuFlag(void);
extern volatile uint16_t _shortFire;
extern volatile uint16_t _fire;
extern uint16_t  _controlMask;
extern uint8_t   _currentMenu[];

/* input_sdl.c */
extern void        InputSDL_CaptureKey(int on);
extern int32_t     InputSDL_CapturedKey(void);
extern void        InputSDL_ClearKeyState(void);
extern const char *InputSDL_PlayerPref(int p);
extern void        InputSDL_SetPlayerPref(int p, const char *pref);
extern int         InputSDL_PlayerDevice(int p);
extern int         InputSDL_DeviceOpen(int d);
extern int         InputSDL_MaxDevices(void);
extern void        InputSDL_DeviceLabel(int d, char *out, int cap);
extern void        InputSDL_DevicePref(int d, char *out, int cap);
extern void        InputSDL_DeviceInfo(int d, char guid[33], int *id, int *buttons, int *axes, const char **power,
                                       int *rawX, int *rawY, int dirs[4]);
extern int         InputSDL_PadButton(int d, int bench);
extern void        InputSDL_PadButtonName(int d, int bench, int b, char *out, int cap);
extern int         InputSDL_SetPadButton(int d, int bench, int b);
extern int         InputSDL_PadCut(int d, int k);
extern void        InputSDL_SetPadCut(int d, int k, int pct);
extern int         InputSDL_PadStick(int d);
extern void        InputSDL_SetPadStick(int d, int bits);
extern void        InputSDL_PadStickTurn(int d, int *x, int *y);
extern int         InputSDL_StickDeadzonePercent(void);
extern void        InputSDL_SetStickDeadzone(int percent);
extern void        InputSDL_PadPressBegin(void);
extern int         InputSDL_AnyPadButtonHeld(void);
extern void        InputSDL_PostFrame(void);
extern int         InputSDL_PadPressPoll(int only, int *dev, int *button);
extern int         InputSDL_PadButtonDown(int d, int b);
extern void        InputSDL_GivePlayerDevice(int p, int d);
extern int         InputSDL_AutoDetect(void);
extern void        InputSDL_SetAutoDetect(int on);
extern int         InputSDL_ShouldersOff(void);
extern void        InputSDL_SetShouldersOff(int on);
extern int         InputSDL_CallBench(int player);
extern void        InputSDL_SetCallBench(int player, int mode);
extern void        InputSDL_StoreConfig(PlatformInitOptions *o);
extern int         InputSDL_TakeQuickSave(void);

static MenuEntry *entry_at(int32_t ordinal)
{
    return CalcMenuEntryAddress((uint16_t)ordinal);
}

/* The game's letters are capitals. */
static void upper(char *out, int32_t size, const char *in)
{
    int32_t i;
    for (i = 0; i < size - 1 && in[i]; i++)
        out[i] = (char)((in[i] >= 'a' && in[i] <= 'z') ? in[i] - 32 : in[i]);
    out[i] = '\0';
}

/* SWOS.ini at once, like every row of OPTIONS. */
static void save(void)
{
    if (!g_arcadeOpts)
        return;
    InputSDL_StoreConfig(g_arcadeOpts);
    if (g_arcadeIniPath)
        Settings_Save(g_arcadeOpts, g_arcadeIniPath);
}

static const char *on_off(int on) { return on ? "ON" : "OFF"; }

/* A choice stepped by `dir` (+1 / -1) through `n`, wrapping. */
static int32_t step(int32_t at, int32_t dir, int32_t n)
{
    return ((at + dir) % n + n) % n;
}

/* ===================================================================== the menu look */

/* SWOS 2020's flashing menu cursor (setFlashingMenuCursorVariables): its mode 1 is the frame in
 * one colour, not flashing. Ours: [video] menu_cursor flashing (the game's own) | steady. The
 * cursor's colours are _kColorTableShine (DrawSelectedMenuItemFrame), so steady is the table at
 * its first colour throughout. */
static uint16_t s_shineDefault[8];
static int32_t  s_shineSaved;
static int32_t  s_cursorSteady;

void MenuLook_SetCursor(int steady)
{
    extern uint16_t _kColorTableShine[8];
    int32_t i;
    if (!s_shineSaved) {
        memcpy(s_shineDefault, _kColorTableShine, sizeof s_shineDefault);
        s_shineSaved = 1;
    }
    s_cursorSteady = steady != 0;
    for (i = 0; i < 8; i++)
        _kColorTableShine[i] = s_cursorSteady ? s_shineDefault[0] : s_shineDefault[i];
}

/* ===================================================================== a page of rows */

typedef struct {
    const char *label;
    void (*show)(char *out, int32_t cap);
    void (*change)(int32_t dir);            /* +1: fire or right, -1: left */
    uint16_t    labelColor;                 /* its label's background, kLabel* (0: the page's grey) */
    int       (*off)(void);                 /* non-zero: greyed out, nothing to choose (NULL: never) */
} Row;

/* A label's background (settingsPage.json's labels are grey, 7): CONTROLS tells player 1's rows
 * from player 2's by colour as well as by name. */
enum { kWaitColour = 15 };      /* a row waiting for a press: yellow (the Amiga look; PC: yellow-green) */
enum { kLabelGrey = 7, kLabelPlayer1 = 11, kLabelPlayer2 = 10 };
enum { kValueOn = 13, kValueOff = 9 };  /* a value's background (settingsPage.json's 13), greyed (Row.off) */   /* (purple -- the Amiga look's blue -- and red) */

typedef struct {
    const char *title;
    const Row  *rows;
    int32_t     count;
} Page;

enum { kRows = 12, kValueLen = 32 };
enum {                                      /* settingsPage.json's entries */
    Page_header,
    Page_label0,                            /* label<i> = label0 + 2i, value<i> = label0 + 2i + 1 */
    Page_note = Page_label0 + 2 * kRows,
    Page_exit,
    Page_kNumEntries
};

static const Page *s_page;
static char s_values[kRows][kValueLen];
static char s_note[96];

static void note(const char *text)
{
    snprintf(s_note, sizeof s_note, "%s", text);
}

/* A change that takes hold only at the next start -- here, OPTIONS' LANGUAGE (menus.c), a mod
 * turned on or off (mods_menu.c): leaving the screen asks whether to start the game again now
 * (Options_AskRestart), the same question wherever it was made. */
static int32_t s_restartPending;
void Options_RestartNeeded(void) { s_restartPending = 1; }

static void later(void)
{
    s_restartPending = 1;
    note("FROM THE NEXT START");
}

/* A setting only the start reads (SKIP INTRO): the next start's, and no restart asked for --
 * starting again would only show what it is about. */
static void next_start_only(void)
{
    note("FROM THE NEXT START");
}

static void page_refresh(void)
{
    int32_t i;
    if (!s_page)
        return;
    entry_at(Page_header)->fg = (uintptr_t)s_page->title;
    for (i = 0; i < kRows; i++) {
        MenuEntry *label = entry_at(Page_label0 + 2 * i), *value = entry_at(Page_label0 + 2 * i + 1);
        label->isInvisible = value->isInvisible = i >= s_page->count;
        if (i >= s_page->count)
            continue;
        s_page->rows[i].show(s_values[i], kValueLen);
        label->fg = (uintptr_t)s_page->rows[i].label;
        label->bgColor = s_page->rows[i].labelColor ? s_page->rows[i].labelColor : kLabelGrey;
        value->fg = (uintptr_t)s_values[i];
        if (s_page->rows[i].off)        /* (only such rows: SELECT JOYPAD's wait colours its own) */
            value->bgColor = (uint16_t)((value->bgColor & 0xFFF0) | (s_page->rows[i].off() ? kValueOff : kValueOn));
    }
    entry_at(Page_note)->fg = (uintptr_t)s_note;
}

static void nav(int32_t ordinal, int32_t up, int32_t down)
{
    MenuEntry *e = entry_at(ordinal);
    e->upEntry = (int8_t)up;
    e->downEntry = (int8_t)down;
    e->directionUp = e->directionDown = -1;
}

static void settingsPage_OnInit(void)
{
    int32_t i, n = s_page ? s_page->count : 0;
    for (i = 0; i < n; i++) {
        const int32_t value = Page_label0 + 2 * i + 1;
        nav(value, i ? value - 2 : -1, i + 1 < n ? value + 2 : Page_exit);
    }
    nav(Page_exit, n ? Page_label0 + 2 * n - 1 : -1, -1);
    page_refresh();
}

static int32_t s_fireHeld;              /* fire pressed on this page and held since (page_row) */

static void settingsPage_OnDraw(void)
{
    if (!_fire)
        s_fireHeld = 0;
    page_refresh();
}

static int32_t s_activeRow = -1;           /* the row whose change is running (page_row) */

/* As in the original game, only fire changes a value: a press one step on, held it runs on --
 * kHoldFirstMs after the press, then every kHoldEveryMs. The stick does nothing here (its rows'
 * mask: fire pressed, 0x20, and held, 0x01): a shaky stick changed values by itself (2026-11-25).
 * Rows that open a screen of their own or move the window are not run on (no_repeat). */
enum { kHoldFirstMs = 400, kHoldEveryMs = 150 };
static Uint32 s_holdNext;

static void p1_select(int32_t dir), p2_select(int32_t dir), p1_setup(int32_t dir), p2_setup(int32_t dir),
            keys_change(int32_t dir), winmode_change(int32_t dir), scale_change(int32_t dir),
            monitor_change(int32_t dir), res_change(int32_t dir), renderer_change(int32_t dir);

static int32_t no_repeat(void (*change)(int32_t))
{
    static void (*const k[])(int32_t) = {
        p1_select, p2_select, p1_setup, p2_setup, keys_change,
        winmode_change, scale_change, monitor_change, res_change, renderer_change,
    };
    for (uint32_t i = 0; i < sizeof k / sizeof k[0]; i++)
        if (k[i] == change)
            return 1;
    return 0;
}

static void page_row(int32_t i)
{
    const int32_t fire = (_controlMask & 0x20) != 0;    /* (0x20 fire pressed, 0x01 held) */
    const int32_t dir = 1;
    char before[48], after[48];
    if (!s_page || i >= s_page->count)
        return;
    if (fire) {
        s_fireHeld = 1;
        s_holdNext = SDL_GetTicks() + kHoldFirstMs;
    } else {                            /* (held: on, after a while, where it may) */
        if (!s_fireHeld || no_repeat(s_page->rows[i].change) || !SDL_TICKS_PASSED(SDL_GetTicks(), s_holdNext))
            return;
        s_holdNext = SDL_GetTicks() + kHoldEveryMs;
    }
    note("");
    s_page->rows[i].show(before, (int32_t)sizeof before);
    s_activeRow = i;
    s_page->rows[i].change(dir);
    s_activeRow = -1;
    s_page->rows[i].show(after, (int32_t)sizeof after);
    fprintf(stderr, "[settings] %s: %s %s -> %s (%s)\n", s_page->title, s_page->rows[i].label,
            before, after, fire ? "fire" : "fire held");
    page_refresh();
}

static void settingsPage_row0_OnSelect(void)  { page_row(0); }
static void settingsPage_row1_OnSelect(void)  { page_row(1); }
static void settingsPage_row2_OnSelect(void)  { page_row(2); }
static void settingsPage_row3_OnSelect(void)  { page_row(3); }
static void settingsPage_row4_OnSelect(void)  { page_row(4); }
static void settingsPage_row5_OnSelect(void)  { page_row(5); }
static void settingsPage_row6_OnSelect(void)  { page_row(6); }
static void settingsPage_row7_OnSelect(void)  { page_row(7); }
static void settingsPage_row8_OnSelect(void)  { page_row(8); }
static void settingsPage_row9_OnSelect(void)  { page_row(9); }
static void settingsPage_row10_OnSelect(void) { page_row(10); }
static void settingsPage_row11_OnSelect(void) { page_row(11); }
static void settingsPage_exit_OnSelect(void)  { SetExitMenuFlag(); }

/* Draw the note now, over its own background: for the prompts that wait inside a row. */
static void note_now(const char *text)
{
    MenuEntry *n = entry_at(Page_note);
    note(text);
    n->fg = (uintptr_t)s_note;
    DrawMenuItemBackgroundMasked(n->x, n->y, n->dwWidth, n->dwHeight);
    CallDrawMenuItem(n);
}

/* After a prompt answered from a pad: the button let go, and its press no menu's. */
/* After a press a row took for itself (a button set, a pad chosen): wait until it is let go --
 * every button of every pad, not only what reads as fire now (a button just made BENCH no longer
 * does, but it was fire when it went down) -- then forget it, so its release is not a fire on the
 * same row that asks for a button again. At most ~3 s. */
static void release_wait(int dev, int button)
{
    /* Real time, with SDL's input brought up to date: a menu read alone runs no frame (the
     * pads' state and the registers stay what they were), so the loop used to end at once with
     * the button still down -- and the row asked again. The events stay queued for the menus.
     * `dev` / `button`: the press the row took (button -1: that pad's fire) -- only that one is
     * waited for, so a button held all along elsewhere (another pad, a contact an adapter reads as
     * pressed) no longer holds the row up for the full three seconds. dev -1: every pad's;
     * -2: none (the row was left from the keyboard). */
    const Uint32 until = SDL_GetTicks() + 3000;
    for (;;) {
        const Uint8 *keys;
        SDL_PumpEvents();
        keys = SDL_GetKeyboardState(NULL);
        if (!(dev >= 0 ? InputSDL_PadButtonDown(dev, button) : dev == -1 && InputSDL_AnyPadButtonHeld()) &&
            !keys[SDL_SCANCODE_RETURN] && !keys[SDL_SCANCODE_LCTRL] &&
            !keys[SDL_SCANCODE_RCTRL] && !keys[SDL_SCANCODE_SPACE])
            break;
        if (SDL_TICKS_PASSED(SDL_GetTicks(), until))
            break;
        SDL_Delay(10);
    }
    /* Everything up now: the registers the menus read made to say so too (the input step, as a
     * frame runs it), then read twice -- so the game's own press/release tracking sees no press,
     * and no release of the old one either, that would count as a tap. */
    InputSDL_ClearKeyState();
    InputSDL_PostFrame();
    MenuCheckControls();
    MenuCheckControls();
    _fire = 0;
    _shortFire = 0;
    _controlMask = 0;
}

/* ===================================================================== CONTROLS */

static int32_t s_setupDev = -1;     /* CONTROLLER SETUP's pad */

static void control_show(int32_t p, char *out, int32_t cap)
{
    const char *pref = InputSDL_PlayerPref(p);
    const int d = InputSDL_PlayerDevice(p);
    char name[64];
    if (d >= 0)
        InputSDL_DeviceLabel(d, name, sizeof name);
    if (!pref[0]) {
        if (d >= 0) {
            char line[80];
            snprintf(line, sizeof line, "AUTO: %s", name);
            upper(out, cap, line);
        } else
            snprintf(out, (size_t)cap, "%s", p ? "AUTO: NO PAD" : "AUTO: KEYBOARD");
    } else if (!SDL_strcasecmp(pref, "keyboard") || !SDL_strcasecmp(pref, "none"))
        upper(out, cap, pref);
    else if (d >= 0)
        upper(out, cap, name);
    else
        snprintf(out, (size_t)cap, "NOT CONNECTED");
}

/* PLAYER n: AUTO -> KEYBOARD (player 1) / NONE (player 2) -> each pad -> AUTO (SWOS 2020's list
 * of controls, pl1SelectControls / pl2SelectControls). */
static void control_change(int32_t p, int32_t dir)
{
    char opts[2 + 8][48];
    int32_t n = 0, at = 0, i, d;
    const char *cur = InputSDL_PlayerPref(p);

    snprintf(opts[n++], sizeof opts[0], "%s", "");
    snprintf(opts[n++], sizeof opts[0], "%s", p ? "none" : "keyboard");
    for (d = 0; d < InputSDL_MaxDevices() && n < (int32_t)(sizeof opts / sizeof opts[0]); d++)
        if (InputSDL_DeviceOpen(d))
            InputSDL_DevicePref(d, opts[n++], (int)sizeof opts[0]);
    for (i = 0; i < n; i++)
        if (!SDL_strcasecmp(opts[i], cur))
            at = i;
    at = step(at, dir, n);
    InputSDL_SetPlayerPref(p, opts[at]);
    save();
}

static void select_show(char *out, int32_t cap) { snprintf(out, (size_t)cap, "PRESS FIRE"); }

/* SELECT JOYPAD (SWOS 2020's selectJoypadWithButtonPress): the next pad a button is pressed on
 * is this player's. ESC leaves it as it was. */
static void select_joypad(int32_t p)
{
    int dev = -1, button = -1, found = 0, any = 0;
    char line[96];
    for (int d = 0; d < InputSDL_MaxDevices(); d++)
        any |= InputSDL_DeviceOpen(d);
    if (!any) {
        note("NO JOYPAD IS CONNECTED");
        return;
    }
    snprintf(line, sizeof line, "PRESS A BUTTON ON PLAYER %d'S PAD   ESC: LEAVE IT", (int)p + 1);
    note_now(line);
    /* The press that chose the row (a pad's fire or a key) only starts the wait -- it is held as
     * the wait begins, so it does not count: the NEXT press picks the pad, whichever pad it is on
     * (a row chosen with one pad can give player 1 another). */
    InputSDL_PadPressBegin();
    InputSDL_CaptureKey(1);
    /* Waiting: the row's value box in yellow (15) until a pad answers or ESC, so it shows that it
     * waits for a press (its own light blue, 13, back after). */
    MenuEntry *waitBox = !found && s_activeRow >= 0 ? entry_at(Page_label0 + 2 * s_activeRow + 1) : NULL;
    const uint16_t waitBoxWas = waitBox ? waitBox->bgColor : 0;
    if (waitBox) {
        waitBox->bgColor = kWaitColour;
        CallDrawMenuItem(waitBox);
    }
    for (; !found;) {
        MenuCheckControls();
        if (InputSDL_CapturedKey() == SDL_SCANCODE_ESCAPE)
            break;
        if (InputSDL_CapturedKey() != SDL_SCANCODE_UNKNOWN)
            InputSDL_CaptureKey(1);         /* (another key: still waiting) */
        if (InputSDL_PadPressPoll(-1, &dev, &button)) {
            found = 1;
            break;
        }
        CallDrawMenuItem(entry_at(Page_note));
    }
    InputSDL_CaptureKey(0);
    if (waitBox) {
        waitBox->bgColor = waitBoxWas;
        CallDrawMenuItem(waitBox);
    }
    fprintf(stderr, "[settings] SELECT JOYPAD, player %d: %s\n", (int)p + 1, found ? "a pad chosen" : "left");
    if (found) {
        char name[64];
        InputSDL_GivePlayerDevice(p, dev);
        InputSDL_DeviceLabel(dev, name, sizeof name);
        snprintf(line, sizeof line, "%s SELECTED", name);
        upper(s_note, sizeof s_note, line);
        save();
        page_refresh();                 /* (shown now, not once the button is let go) */
        note_now(s_note);
    } else
        note("");
    release_wait(found ? dev : -2, found ? button : -1);
}

static void setup_show(int32_t p, char *out, int32_t cap)
{
    snprintf(out, (size_t)cap, "%s", InputSDL_PlayerDevice(p) >= 0 ? "BUTTONS, STICK" : "NO PAD");
}

static void joypad_setup(int32_t p)
{
    uint8_t *menu;
    s_setupDev = InputSDL_PlayerDevice(p);
    if (s_setupDev < 0) {
        note(p ? "PLAYER 2 HAS NO PAD" : "PLAYER 1 PLAYS WITH THE KEYBOARD ALONE");
        return;
    }
    menu = MenuJson_Load(JOYPAD_CONFIG_NAME);
    if (!menu) {
        fprintf(stderr, "[settings] cannot show %s\n", JOYPAD_CONFIG_NAME);
        return;
    }
    ShowMenu(menu);
}

static void p1_show(char *o, int32_t c)       { control_show(0, o, c); }
static void p2_show(char *o, int32_t c)       { control_show(1, o, c); }
static void p1_change(int32_t dir)            { control_change(0, dir); }
static void p2_change(int32_t dir)            { control_change(1, dir); }
static void p1_select(int32_t dir)            { (void)dir; select_joypad(0); }
static void p2_select(int32_t dir)            { (void)dir; select_joypad(1); }
static void p1_setup_show(char *o, int32_t c) { setup_show(0, o, c); }
static void p2_setup_show(char *o, int32_t c) { setup_show(1, o, c); }
static void p1_setup(int32_t dir)             { (void)dir; joypad_setup(0); }
static void p2_setup(int32_t dir)             { (void)dir; joypad_setup(1); }

static void keys_show(char *o, int32_t c)     { snprintf(o, (size_t)c, "REDEFINE"); }
static void keys_change(int32_t dir)
{
    extern void ControlsMenu_Show(void);    /* the keyboard's keys (src/controls_menu.c) */
    (void)dir;
    ControlsMenu_Show();
}

static void autodetect_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", on_off(InputSDL_AutoDetect())); }
static void autodetect_change(int32_t dir)
{
    (void)dir;
    InputSDL_SetAutoDetect(!InputSDL_AutoDetect());
    note(InputSDL_AutoDetect() ? "A PAD PLUGGED IN GOES TO A FREE PLAYER" : "A PAD PLUGGED IN WAITS FOR SELECT JOYPAD");
    save();
}

static void shoulders_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", on_off(InputSDL_ShouldersOff())); }
static void shoulders_change(int32_t dir)
{
    (void)dir;
    InputSDL_SetShouldersOff(!InputSDL_ShouldersOff());
    note(InputSDL_ShouldersOff() ? "SHOULDERS AND STICK CLICKS ARE NO FIRE OR BENCH"
                                 : "SHOULDERS CAN BE FIRE OR BENCH");
    save();
}

/* CALL BENCH, each player's own: how he calls his bench in every match he plays, offline and
 * online, in any gameplay style -- the second fire button and three taps (the PC's way), the taps
 * alone (the Amiga's) or the button alone. Online the lobby takes it to the match setup. */
static void bench_show_for(int player, char *o, int32_t c)
{
    static const char *const k[3] = { "BUTTON OR TAPS", "TAPS ONLY", "BUTTON ONLY" };
    snprintf(o, (size_t)c, "%s", k[InputSDL_CallBench(player)]);
}
static void bench_change_for(int player, int32_t dir)
{
    static const char *const k[3] = { "THE 2ND FIRE BUTTON OR THREE TAPS", "THREE TAPS, AS ON THE AMIGA",
                                      "THE 2ND FIRE BUTTON ALONE" };
    const int mode = (int)step(InputSDL_CallBench(player), dir, 3);
    InputSDL_SetCallBench(player, mode);
    note(k[mode]);
    save();
}
static void bench1_show(char *o, int32_t c) { bench_show_for(1, o, c); }
static void bench1_change(int32_t dir)      { bench_change_for(1, dir); }
static void bench2_show(char *o, int32_t c) { bench_show_for(2, o, c); }
static void bench2_change(int32_t dir)      { bench_change_for(2, dir); }

static void deadzone_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%d%%", InputSDL_StickDeadzonePercent()); }
static void deadzone_change(int32_t dir)
{
    int v = InputSDL_StickDeadzonePercent() + 5 * dir;
    v = v < 5 ? 90 : v > 90 ? 5 : v;
    InputSDL_SetStickDeadzone(v);
    if (g_arcadeOpts)
        g_arcadeOpts->stick_deadzone = v;
    note("HOW FAR A STICK MUST GO BEFORE IT COUNTS");
    save();
}

/* MOUSE POINTER: [input] mouse_cursor (src/mouse_cursors.c; launcher options, agent A). */
static void pointer_show(char *o, int32_t c)
{
    extern int32_t MouseCursors_Style(void);
    static const char *const k[3] = { "SYSTEM", "WORKBENCH 2.04", "WORKBENCH 1.X" };
    snprintf(o, (size_t)c, "%s", k[MouseCursors_Style()]);
}
static void pointer_change(int32_t dir)
{
    extern int32_t MouseCursors_Style(void);
    extern void MouseCursors_Apply(int32_t style);
    MouseCursors_Apply(step(MouseCursors_Style(), dir, 3));
    if (g_arcadeOpts)
        g_arcadeOpts->mouse_cursor = MouseCursors_Style();
    note("THE MOUSE POINTER IN THE MENUS");
    save();
}

static const Row k_controlRows[] = {
    { "PLAYER 1",                 p1_show,         p1_change,     kLabelPlayer1 },
    { "P1 SELECT JOYPAD",         select_show,     p1_select,     kLabelPlayer1 },
    { "P1 CONTROLLER SETUP",      p1_setup_show,   p1_setup,      kLabelPlayer1 },
    { "P1 CALL BENCH",            bench1_show,     bench1_change, kLabelPlayer1 },
    { "PLAYER 2",                 p2_show,         p2_change,     kLabelPlayer2 },
    { "P2 SELECT JOYPAD",         select_show,     p2_select,     kLabelPlayer2 },
    { "P2 CONTROLLER SETUP",      p2_setup_show,   p2_setup,      kLabelPlayer2 },
    { "P2 CALL BENCH",            bench2_show,     bench2_change, kLabelPlayer2 },
    { "KEYBOARD KEYS",            keys_show,       keys_change },
    { "AUTO-DETECT JOYPADS",      autodetect_show, autodetect_change },
    { "DISABLE SHOULDER BUTTONS", shoulders_show,  shoulders_change },
    { "STICK DEAD ZONE",          deadzone_show,   deadzone_change },
};
static const Page k_controlsPage = { "CONTROLS", k_controlRows, (int32_t)(sizeof k_controlRows / sizeof k_controlRows[0]) };
_Static_assert(sizeof k_controlRows / sizeof k_controlRows[0] <= kRows, "a settings page holds kRows rows at most");

/* ===================================================================== VIDEO */

static PlatformInitOptions s_none;      /* (no options published: nothing to show, nothing kept) */
static PlatformInitOptions *O(void) { return g_arcadeOpts ? g_arcadeOpts : &s_none; }

static void winmode_show(char *o, int32_t c)
{
    static const char *const k[3] = { "WINDOWED", "FULLSCREEN", "EXCLUSIVE FULLSCREEN" };
    snprintf(o, (size_t)c, "%s", k[Platform_GetFullscreenMode()]);
}
static void winmode_change(int32_t dir)
{
    const PlatformFullscreenMode m = (PlatformFullscreenMode)step((int32_t)Platform_GetFullscreenMode(), dir, 3);
    Platform_SetFullscreenMode(m);
    O()->fullscreen = m;
    save();
}

/* WINDOW SCALE: AUTO (the most whole times the picture fits the monitor), then 1X up to that
 * most -- a bigger one would not fit (Platform_WindowScaleMax). */
static void scale_show(char *o, int32_t c)
{
    if (Platform_WindowScaleAuto())
        snprintf(o, (size_t)c, "AUTO (%dX)", Platform_GetWindowScale());
    else
        snprintf(o, (size_t)c, "%dX", Platform_GetWindowScale());
}
static void scale_change(int32_t dir)
{
    const int most = Platform_WindowScaleMax();
    const int now = Platform_WindowScaleAuto() ? 0 : Platform_GetWindowScale();
    const int s = (int)step(now > most ? most : now, dir, most + 1);    /* (0 AUTO, 1..most) */
    Platform_SetWindowScale(s);
    O()->win_scale = s;
    if (Platform_GetFullscreenMode() != PLATFORM_FULLSCREEN_NONE)
        note("THE WINDOW'S SIZE, WHEN IT IS ONE");
    save();
}

static void vsync_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", on_off(O()->vsync)); }
static void vsync_change(int32_t dir) { (void)dir; O()->vsync = !O()->vsync; later(); save(); }

static void wide_show(char *o, int32_t c)
{
    static const char *const k[3] = { "OFF", "ON", "STRETCH" };
    snprintf(o, (size_t)c, "%s", k[O()->widescreen >= 0 && O()->widescreen <= 2 ? O()->widescreen : 0]);
}
static void wide_change(int32_t dir) { O()->widescreen = step(O()->widescreen, dir, 3); later(); save(); }

static void scroll_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", on_off(O()->subpixel_scroll)); }
static void scroll_change(int32_t dir) { (void)dir; O()->subpixel_scroll = !O()->subpixel_scroll; Platform_SetSubpixelScroll(O()->subpixel_scroll); save(); }

static void scaling_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", O()->scaling_smooth ? "SMOOTH" : "SHARP"); }
static void scaling_change(int32_t dir) { (void)dir; O()->scaling_smooth = !O()->scaling_smooth; Platform_SetScalingSmooth(O()->scaling_smooth); save(); }

/* MONITOR: the one the game is on and opens on ([video] screen), its size beside its number;
 * a change moves the window there at once (Platform_MoveToDisplay). One monitor: nothing to pick. */
static void monitor_show(char *o, int32_t c)
{
    const int n = Platform_DisplayCount();
    const int d = O()->screen >= 0 && O()->screen < n ? O()->screen : 0;
    char size[24];
    Platform_DisplayDesc(d, size, (int)sizeof size);
    snprintf(o, (size_t)c, "%d: %s", d + 1, size);
}
static void monitor_change(int32_t dir)
{
    const int n = Platform_DisplayCount();
    if (n < 2) {
        note("ONLY ONE MONITOR");
        return;
    }
    O()->screen = step(O()->screen >= 0 && O()->screen < n ? O()->screen : 0, dir, n);
    Platform_MoveToDisplay(O()->screen);
    note("THE GAME IS ON IT NOW, AND STARTS THERE");
    save();
}

static void renderer_show(char *o, int32_t c)
{
    static const char *const k[3] = { "SOFTWARE", "GPU", "SDL" };
    snprintf(o, (size_t)c, "%s", k[O()->renderer >= 0 && O()->renderer <= 2 ? O()->renderer : 1]);
}
static void renderer_change(int32_t dir) { O()->renderer = (PlatformRendererMode)step((int32_t)O()->renderer, dir, 3); later(); save(); }

static void cursor_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", s_cursorSteady ? "STEADY" : "FLASHING"); }
static void cursor_change(int32_t dir)
{
    (void)dir;
    MenuLook_SetCursor(!s_cursorSteady);
    O()->menu_cursor = s_cursorSteady;
    save();
}

/* ---- launcher options (agent A): the SWOS United Launcher's, in the game ---- */

/* RESOLUTION: [video] resolution -- the render's SHAPE (swos_settings.c), auto (the display's) or
 * one of the common shapes; a hand-written WxH shows as it is and steps to AUTO. */
static const struct { const char *name; int w, h; } k_shapes[] = {
    { "AUTO", 0, 0 }, { "4:3", 1440, 1080 }, { "16:10", 1920, 1200 }, { "16:9", 1920, 1080 },
    { "21:9", 2560, 1080 }, { "32:9", 3840, 1080 },
};
enum { kShapes = (int)(sizeof k_shapes / sizeof k_shapes[0]) };

static int32_t shape_index(void)
{
    for (int32_t i = 0; i < kShapes; i++)
        if (O()->res_w == k_shapes[i].w && O()->res_h == k_shapes[i].h)
            return i;
    if (O()->res_w <= 0 || O()->res_h <= 0)
        return 0;
    return -1;
}

/* The display the game's window is on: its desktop size (16:9 when it can't be had). */
static void display_size(int *dw, int *dh)
{
    SDL_DisplayMode dm;
    SDL_Window *w = SDL_GetKeyboardFocus();        /* (the game's window: it has the keys) */
    const int d = w ? SDL_GetWindowDisplayIndex(w) : O()->screen;
    *dw = 16;
    *dh = 9;
    if (SDL_GetDesktopDisplayMode(d >= 0 ? d : 0, &dm) == 0 && dm.w > 0 && dm.h > 0) {
        *dw = dm.w;
        *dh = dm.h;
    }
}

/* AUTO's shape, as WINDOW SCALE's AUTO shows its factor: the nearest of the shapes when it is
 * within 3 % of one (3440x1440 is 21:9, as sold), else the display's own, reduced. */
static void auto_shape(char *o, int32_t c)
{
    int dw, dh;
    display_size(&dw, &dh);
    const double r = (double)dw / dh;
    for (int32_t i = 1; i < kShapes; i++) {
        const double k = (double)k_shapes[i].w / k_shapes[i].h;
        if (r > k * 0.97 && r < k * 1.03) {
            snprintf(o, (size_t)c, "AUTO (%s)", k_shapes[i].name);
            return;
        }
    }
    int a = dw, b = dh;
    while (b) { const int t = a % b; a = b; b = t; }
    snprintf(o, (size_t)c, "AUTO (%d:%d)", dw / a, dh / a);
}

static void res_show(char *o, int32_t c)
{
    const int32_t i = shape_index();
    if (i == 0)
        auto_shape(o, c);
    else if (i > 0)
        snprintf(o, (size_t)c, "%s", k_shapes[i].name);
    else
        snprintf(o, (size_t)c, "%dX%d", O()->res_w, O()->res_h);
}
static void res_change(int32_t dir)
{
    extern int Video_ResolutionChanged(const PlatformInitOptions *o, int dw, int dh);
    int32_t i = shape_index();
    int dw = 16, dh = 9;
    i = i < 0 ? 0 : step(i, dir, kShapes);
    O()->res_w = k_shapes[i].w;
    O()->res_h = k_shapes[i].h;
    if (O()->res_w > 0) {
        dw = O()->res_w;
        dh = O()->res_h;
    } else {
        display_size(&dw, &dh);
    }
    Platform_SetStretchShape(O()->res_w, O()->res_h);  /* (STRETCH: the window takes it) */
    if (Video_ResolutionChanged(O(), dw, dh))
        note(i ? "THE PICTURE'S SHAPE: MORE PITCH WHEN WIDER" : "THE DISPLAY'S OWN SHAPE");
    else if (O()->widescreen == 2)
        note(i ? "THE WINDOW'S SHAPE, THE PICTURE STRETCHED OVER IT" : "THE DISPLAY'S OWN SHAPE");
    else
        note("THE PICTURE'S SHAPE, WITH WIDESCREEN ON");
    save();
}

/* AUTO CENTERING: [video] auto_centering (platform_sdl.c Platform_SetAutoCentering). */
static void centre_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", on_off(O()->auto_centering)); }
static void centre_change(int32_t dir)
{
    extern void Platform_SetAutoCentering(int on);
    (void)dir;
    O()->auto_centering = !O()->auto_centering;
    Platform_SetAutoCentering(O()->auto_centering);
    note("THE WINDOW KEPT IN THE MIDDLE OF ITS DISPLAY");
    save();
}

/* SHOW FPS: [video] show_fps (src/fps_overlay.c). */
static void fps_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", on_off(O()->show_fps)); }
static void fps_change(int32_t dir)
{
    extern void FpsOverlay_Set(int32_t on);
    (void)dir;
    O()->show_fps = !O()->show_fps;
    FpsOverlay_Set(O()->show_fps);
    note("THE FRAMES SHOWN A SECOND, TOP RIGHT");
    save();
}

static void filter_show(char *o, int32_t c);       /* (APPEARANCE OPTIONS' section, below) */
static void filter_change(int32_t dir);
static const Row k_videoRows[] = {
    { "WINDOW MODE",        winmode_show,  winmode_change },
    { "WINDOW SCALE",       scale_show,    scale_change },
    { "VSYNC",              vsync_show,    vsync_change },
    { "WIDESCREEN",         wide_show,     wide_change },
    { "SMOOTH SCROLLING",   scroll_show,   scroll_change },
    { "SCALING",            scaling_show,  scaling_change },
    { "PIXEL FILTER",       filter_show,   filter_change },     /* (beside SCALING: APPEARANCE's until 2026-11-24) */
    { "MONITOR",            monitor_show,  monitor_change },
    { "RENDERER",           renderer_show, renderer_change },
    /* (launcher options, agent A) */
    { "ASPECT RATIO",       res_show,      res_change },     /* ([video] resolution: the shape, not a size) */
    { "AUTO CENTERING",     centre_show,   centre_change },
    { "SHOW FPS",           fps_show,      fps_change },
};
static const Page k_videoPage = { "VIDEO OPTIONS", k_videoRows, (int32_t)(sizeof k_videoRows / sizeof k_videoRows[0]) };
_Static_assert(sizeof k_videoRows / sizeof k_videoRows[0] <= kRows, "a settings page holds kRows rows at most");

/* ===================================================================== AUDIO */

extern uint16_t _menuMusic;
extern int _gNoAmigaSound;

static void volumes_apply(void)
{
    extern void AudioSDL_SetKindVolumes(int music, int chants, int commentary, int effects);
    PlatformInitOptions *o = O();
    AudioSDL_SetKindVolumes(o->vol_music, o->vol_chants, o->vol_commentary, o->vol_effects);
    save();
}

static void vol_show(int v, char *o, int32_t c) { snprintf(o, (size_t)c, "%d%%", v); }
static void vol_change(int *v, int32_t dir)
{
    *v += 10 * dir;
    *v = *v < 0 ? 100 : *v > 100 ? 0 : *v;
    volumes_apply();
}
static void volm_show(char *o, int32_t c) { vol_show(O()->vol_music, o, c); }
static void volc_show(char *o, int32_t c) { vol_show(O()->vol_chants, o, c); }
static void volt_show(char *o, int32_t c) { vol_show(O()->vol_commentary, o, c); }
static void vole_show(char *o, int32_t c) { vol_show(O()->vol_effects, o, c); }
static void volm_change(int32_t d) { vol_change(&O()->vol_music, d); }
static void volc_change(int32_t d) { vol_change(&O()->vol_chants, d); }
static void volt_change(int32_t d) { vol_change(&O()->vol_commentary, d); }
static void vole_change(int32_t d) { vol_change(&O()->vol_effects, d); }

static void menumusic_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", on_off(_menuMusic)); }
static void menumusic_change(int32_t dir)
{
    extern void ChangeMenuMusic(MenuEntry *entry);      /* OPTIONS' row: the music, and [options] */
    (void)dir;
    ChangeMenuMusic(entry_at(Page_note));
}

/* The menus' music again, in the style chosen: StopMusic, then the menu's song. */
static void music_restart(void)
{
    extern void StopMusic(void);
    extern void TryPlayMusic(void);
    if (!_menuMusic)
        return;
    StopMusic();
    TryPlayMusic();
}

static void sound_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", DosSound_Style() ? "PC" : "AMIGA"); }
static void sound_change(int32_t dir)
{
    (void)dir;
    if (_gNoAmigaSound) {
        note("NO AMIGA SOUND: THE SETUP MADE NONE");
        return;
    }
    DosSound_SetStyle(!DosSound_Style());
    O()->sound_style = DosSound_Style();
    save();
}

static void music_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", DosMusic_Style() ? "PC" : "AMIGA"); }
static void music_change(int32_t dir)
{
    (void)dir;
    if (_gNoAmigaSound) {
        note("NO AMIGA MUSIC: THE SETUP MADE NONE");
        return;
    }
    DosMusic_SetStyle(!DosMusic_Style());
    O()->music_style = DosMusic_Style();
    music_restart();
    save();
}

static void pcmusic_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", DosMusic_MenuRecording() ? "MP3" : "MIDI"); }
static void pcmusic_change(int32_t dir)
{
    (void)dir;
    DosMusic_SetMenuRecording(!DosMusic_MenuRecording());
    O()->menu_recording = DosMusic_MenuRecording();
    if (DosMusic_Style())
        music_restart();
    else
        note("WITH MUSIC: PC");
    save();
}

static void pctitle_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", DosMusic_TitleMidi() ? "MIDI" : "MP3"); }
static void pctitle_change(int32_t dir)
{
    (void)dir;
    DosMusic_SetTitleMidi(!DosMusic_TitleMidi());
    O()->title_midi = DosMusic_TitleMidi();
    note(DosMusic_Style() ? "FROM THE NEXT START" : "WITH MUSIC: PC; FROM THE NEXT START");
    save();
}

static void commentary_show(char *o, int32_t c)
{
    static const char *const k[3] = { "OFF", "ON", "AUTO" };
    snprintf(o, (size_t)c, "%s", k[O()->commentary >= 0 && O()->commentary <= 2 ? O()->commentary : 2]);
}
static void commentary_change(int32_t dir)
{
    O()->commentary = step(O()->commentary, dir, 3);
    DosSound_SetCommentary(O()->commentary);
    note(O()->commentary == 2 ? "AUTO: WITH THE PC SOUND" : "THE PC GAME'S COMMENTARY");
    save();
}

static void mono_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", O()->audio_mono ? "MONO" : "STEREO"); }
static void mono_change(int32_t dir)
{
    extern void AudioSDL_SetMono(int mono);
    (void)dir;
    O()->audio_mono = !O()->audio_mono;
    AudioSDL_SetMono(O()->audio_mono);
    save();
}

static const Row k_audioRows[] = {
    { "MUSIC VOLUME",           volm_show,       volm_change },
    { "CROWD CHANTS VOLUME",    volc_show,       volc_change },
    { "COMMENTARY VOLUME",      volt_show,       volt_change },
    { "BALL AND WHISTLE",       vole_show,       vole_change },
    { "MENU MUSIC",             menumusic_show,  menumusic_change },
    { "MATCH SOUNDS",           sound_show,      sound_change },
    { "MUSIC",                  music_show,      music_change },
    { "PC MENU MUSIC",          pcmusic_show,    pcmusic_change },
    { "PC TITLE MUSIC",         pctitle_show,    pctitle_change },
    { "COMMENTARY",             commentary_show, commentary_change },
    { "OUTPUT",                 mono_show,       mono_change },
};
static const Page k_audioPage = { "AUDIO OPTIONS", k_audioRows, (int32_t)(sizeof k_audioRows / sizeof k_audioRows[0]) };
_Static_assert(sizeof k_audioRows / sizeof k_audioRows[0] <= kRows, "a settings page holds kRows rows at most");

/* ===================================================================== GAMEPLAY */

static void style_show(char *o, int32_t c)
{
    static const char *const k[GAMEPLAY_STYLES] = { "AMIGA", "PC DOS", "XBOX", "AMIGA 60", "MOON" };
    snprintf(o, (size_t)c, "%s", k[_gGameplayStyle >= 0 && _gGameplayStyle < GAMEPLAY_STYLES ? _gGameplayStyle : 0]);
}
static void style_change(int32_t dir)
{
    extern void GameplayStyle_Set(int style, int persist);  /* OPTIONS' GAMEPLAY row (menus.c) */
    GameplayStyle_Set(step(_gGameplayStyle, dir, GAMEPLAY_STYLES), 1);
}

static void viewer_apply(void)
{
    extern void ReplayV2_ViewerConfigure(int32_t overlay, int32_t halfSpeed);
    ReplayV2_ViewerConfigure(O()->replay_frame_overlay, O()->replay_half_speed);
    save();
}

static void autosave_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", on_off(O()->replay_auto_save)); }
static void autosave_change(int32_t dir)
{
    (void)dir;
    extern void ReplayRec_SetAutoSave(int32_t on);
    O()->replay_auto_save = !O()->replay_auto_save;
    ReplayRec_SetAutoSave(O()->replay_auto_save);
    note(O()->replay_auto_save ? "A DATED COPY OF EVERY MATCH" : "LATEST.SRP ONLY");
    save();
}

static void hlspeed_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", O()->replay_half_speed ? "HALF" : "NORMAL"); }
static void hlspeed_change(int32_t dir) { (void)dir; O()->replay_half_speed = !O()->replay_half_speed; viewer_apply(); }

static void overlay_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", on_off(O()->replay_frame_overlay)); }
static void overlay_change(int32_t dir) { (void)dir; O()->replay_frame_overlay = !O()->replay_frame_overlay; viewer_apply(); }

/* ---- launcher options (agent A): the SWOS United Launcher's, in the game ---- */

#if SWOS_TWEAK_STRONGER_GK
/* SWOS 2020's --stronger-goalkeeper-ai: [tweaks] stronger_goalkeeper_ai (include/tweaks.h). Off
 * line only: online and in the rigs the original's keepers play whatever this says. */
static void keepers_show(char *o, int32_t c)
{
    snprintf(o, (size_t)c, "%s", (O()->tweaks & TWEAK_STRONGER_GOALKEEPER_AI) ? "STRONGER" : "ORIGINAL");
}
static void keepers_change(int32_t dir)
{
    (void)dir;
    O()->tweaks ^= TWEAK_STRONGER_GOALKEEPER_AI;
    Tweaks_Set(O()->tweaks);
    note(_gGameplayStyle == GAMEPLAY_DOS || _gGameplayStyle == GAMEPLAY_XBOX
         ? "THE AMIGA STYLES' KEEPERS; OFF LINE ONLY" : "THEY REACH A STEP FURTHER; OFF LINE ONLY");
    save();
}
#endif

/* GAME GRAPHICS: [graphics] style amiga | dos | 2020 -- what the pitch, HUD and menus are drawn from
 * (main.c reads it before anything is loaded: from the next start). */
static int32_t art_index(void)
{
    if (!O()->dos_graphics)
        return 0;
    return strcmp(O()->dos_dir, "grafs_2020") == 0 ? 2 : 1;
}
static void art_show(char *o, int32_t c)
{
    static const char *const k[3] = { "AMIGA", "PC DOS", "SWOS 2020" };
    snprintf(o, (size_t)c, "%s", k[art_index()]);
}
static void art_change(int32_t dir)
{
    extern int swos_data_exists(const char *rel);
    const int32_t i = step(art_index(), dir, 3);
    O()->dos_graphics = i != 0;
    if (i)
        snprintf(O()->dos_dir, sizeof O()->dos_dir, "%s", i == 2 ? "grafs_2020" : "grafs_dos");
    if (i == 2 && !swos_data_exists("grafs_2020/pitch1.dat")) {
        s_restartPending = 1;
        note("NO SWOS 2020 PITCHES: THE PC GAME'S; NEXT START");
    } else
        later();
    save();
}

/* MENU BUTTON LOOK: [mod.dosmenus] look amiga | pc -- the PC menu shading mod's (src/mod_dosmenus.c, which
 * follows it at once; F5 still flips it for the run). */
static void look_show(char *o, int32_t c)
{
    extern const char *ModApi_Setting(const char *slug, const char *key, const char *fallback);
    const char *v = ModApi_Setting("dosmenus", "look", PROJECT_MENU_LOOK_AMIGA ? "amiga" : "pc");
    snprintf(o, (size_t)c, "%s", strcmp(v, "pc") == 0 ? "PC DOS" : "AMIGA");
}
static void look_change(int32_t dir)
{
    extern const char *ModApi_Setting(const char *slug, const char *key, const char *fallback);
    extern int32_t ModApi_SetSetting(const char *slug, const char *key, const char *value);
    extern int32_t Mods_IsOn(const char *slug);
    extern int swos_data_exists(const char *rel);
    const char *v = ModApi_Setting("dosmenus", "look", PROJECT_MENU_LOOK_AMIGA ? "amiga" : "pc");
    (void)dir;
    ModApi_SetSetting("dosmenus", "look", strcmp(v, "pc") == 0 ? "amiga" : "pc");
    if (!Mods_IsOn("dosmenus"))         /* (part of SWOS United: never off there; an OpenSWOS player's may be) */
        note("THE PC MENU SHADING MOD IS OFF: OPTIONS, MODS");
    else if (!swos_data_exists("grafs_dos/FILL.256"))
        note("PC DOS NEEDS THE PC GAME'S DATA");
    else
        note("F5 SWITCHES IT TOO, FOR THIS RUN");
}

/* CPU TACTICS: [gameplay] cpu_tactics (src/tactics_expert.c). Off line only. */
static void tactics_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", O()->cpu_tactics ? "EXPERT" : "NORMAL"); }
static void tactics_change(int32_t dir)
{
    extern void TacticsExpert_Set(int32_t expert);
    (void)dir;
    O()->cpu_tactics = !O()->cpu_tactics;
    TacticsExpert_Set(O()->cpu_tactics);
    note(O()->cpu_tactics ? "SWOS 2020'S IMPROVED FORMATIONS; OFF LINE ONLY" : "THE GAME'S OWN FORMATIONS");
    save();
}

/* START-UP: [general] skip_intro, skip_image_reels, no_load_pause (SWOS 2020's --no-intro,
 * --no-image-reels, --no-load-pause). */
static void intro_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", O()->skip_intro ? "SKIP" : "PLAY"); }
static void intro_change(int32_t dir) { (void)dir; O()->skip_intro = !O()->skip_intro; next_start_only(); save(); }

static void reels_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", O()->skip_image_reels ? "TITLE ONLY" : "ALL"); }
static void reels_change(int32_t dir)
{
    (void)dir;
    O()->skip_image_reels = !O()->skip_image_reels;
    note(O()->skip_image_reels ? "THE TITLE AND ITS TUNE ALONE; NEXT START" : "EVERY PICTURE; NEXT START");
    save();
}

static void loadpause_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", O()->no_load_pause ? "OFF" : "ON"); }
static void loadpause_change(int32_t dir)
{
    extern int _gNoLoadPause;
    (void)dir;
    O()->no_load_pause = !O()->no_load_pause;
    _gNoLoadPause = O()->no_load_pause;
    note(O()->no_load_pause ? "THE STADIUM SCREEN GOES AT ONCE; OFF LINE ONLY" : "THE STADIUM SCREEN'S 3 SECONDS");
    save();
}


/* ===================================================================== DISPLAY OPTIONS
 * (launcher options, agent B; docs/RENDERING.md "DISPLAY OPTIONS") The SWOS United Launcher's look
 * options -- SWOS 2020's command-line flags -- in the game: the menus' opacity, the pixel filter,
 * the improved pitch palette ([video] in SWOS.ini, Platform_SetDisplayOptions:
 * at once), and the result display's and the scoreboard's settings, which are their mods'
 * ([mod.display2020], [mod.scoreboard]: OPTIONS -> MODS turns the mods on). All drawing only. */

static void display_apply(void)
{
    Platform_SetDisplayOptions(O());
    save();
}

static void opacity_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%d%%", O()->menu_opacity); }
static void opacity_change(int32_t dir)
{
    O()->menu_opacity = (int)step(O()->menu_opacity / 10, dir, 11) * 10;
    note("THE MENUS' BUTTONS OVER THE BACKDROP");
    display_apply();
}

static void filter_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", PixelFilter_Label(O()->pixel_filter)); }
static void filter_change(int32_t dir)
{
    O()->pixel_filter = (int)step(O()->pixel_filter, dir, PIXEL_FILTER_COUNT);
    note(O()->renderer == PLATFORM_RENDERER_ACCEL ? "NOT WITH A SHADER PRESET (RETRO TV)" : "THE GPU RENDERER ONLY");
    display_apply();
}

/* SWOS 2020's GK/REF CLASH KITS ([graphics] clash_kits, clash_kits.c): the keepers and the referee
 * in a colour that stands out against a dark kit. Taken at the next match's sprites. */
static void clashkits_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", O()->clash_kits ? "ON" : "OFF"); }
/* Which kits are too alike ([graphics] kit_colours, kit_variants.h): EXTENDED judges by how alike the
 * colours look (CIEDE2000), ORIGINAL by the original's tables. */
static void kitcols_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", O()->kit_colours ? "EXTENDED" : "ORIGINAL"); }
/* MENU BACKGROUND: SWOS United's own picture behind the menus (menu_backgrounds.c), AMIGA (black
 * and purple, the default) or PC (blue and black), at once. Greyed with the Menu backgrounds mod
 * on: its picture is shown then. */
static int menubg_off(void)
{
    extern int32_t Mods_IsOn(const char *slug);
    return Mods_IsOn("backgrounds");
}
static int logo_hd(void)
{
    extern const char *ModApi_Setting(const char *slug, const char *key, const char *fallback);
    return strcmp(ModApi_Setting("menus2020", "logo", "pixel"), "hd") == 0;
}
static void menubg_show(char *o, int32_t c)
{
    snprintf(o, (size_t)c, "%s", menubg_off() ? "MOD" : O()->menu_background == 1 ? "PC BLUE" : "AMIGA PURPLE");
}
/* MENU LOGO: the main menu's logo as its pixels (PIXEL) or drawn smooth at the window's resolution
 * (HD) -- [mod.menus2020] logo, mod_menus2020.c (the mod is part of the game, never listed under
 * MODS, so its one setting is here). Seen as the main menu is come back to. */
static void logo_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", logo_hd() ? "HD" : "PIXEL"); }
static void logo_change(int32_t dir)
{
    extern int32_t ModApi_SetSetting(const char *slug, const char *key, const char *value);
    (void)dir;
    ModApi_SetSetting("menus2020", "logo", logo_hd() ? "pixel" : "hd");
}
static void menubg_change(int32_t dir)
{
    extern void MenuBackgrounds_Choose(int which);
    extern uint8_t _aGrafsMenubgRaw[];
    extern void LoadOverlayToFrontbuffer(const uint8_t *path);
    extern void DrawMenu(void);
    (void)dir;
    if (menubg_off()) {
        note("THE MENU BACKGROUNDS MOD IS ON: ITS PICTURE");
        return;
    }
    O()->menu_background = !O()->menu_background;
    MenuBackgrounds_Choose(O()->menu_background);
    save();
    LoadOverlayToFrontbuffer(_aGrafsMenubgRaw);   /* (the menus' backdrop again, the new one) */
    DrawMenu();
}

static void kitcols_change(int32_t dir)
{
    (void)dir;
    O()->kit_colours = !O()->kit_colours;
    KitVariants_SetRule(O()->kit_colours ? KIT_RULE_EXTENDED : KIT_RULE_ORIGINAL);
    note("WHICH KITS CLASH, FROM THE NEXT MATCH");
    save();
}

static void clashkits_change(int32_t dir)
{
    extern int32_t g_clashKits;
    (void)dir;
    O()->clash_kits = !O()->clash_kits;
    g_clashKits = O()->clash_kits;
    note("KEEPERS AND REFEREE, FROM THE NEXT MATCH");
    save();
}

static void pitchpal_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", O()->improved_pitch_palette ? "IMPROVED" : "ORIGINAL"); }
static void pitchpal_change(int32_t dir)
{
    (void)dir;
    O()->improved_pitch_palette = !O()->improved_pitch_palette;
    note("WET AND SOFT PITCHES, FROM THE NEXT MATCH");
    display_apply();
}

/* A mod's setting, cycled through its choices (`choices` NULL: off / on). */
static void mod_setting_show(const char *slug, const char *key, const char *fallback, char *o, int32_t c)
{
    if (!Mods_IsOn(slug)) {
        snprintf(o, (size_t)c, "MOD OFF");
        return;
    }
    upper(o, c, ModApi_Setting(slug, key, fallback));
}
static void mod_setting_change(const char *slug, const char *key, const char *fallback,
                               const char *const *choices, int32_t n, int32_t dir)
{
    static const char *const k_onOff[2] = { "off", "on" };
    const char *now = ModApi_Setting(slug, key, fallback);
    int32_t at = 0;
    if (!choices) {
        choices = k_onOff;
        n = 2;
    }
    for (int32_t i = 0; i < n; i++)
        if (SDL_strcasecmp(now, choices[i]) == 0)
            at = i;
    ModApi_SetSetting(slug, key, choices[step(at, dir, n)]);
    note(Mods_IsOn(slug) ? "" : "TURN THE MOD ON IN OPTIONS, MODS");
}

static const char *const k_bands[3] = { "smooth", "game", "off" };
static const char *const k_corners[4] = { "top-left", "top-right", "bottom-left", "bottom-right" };
static void reskits_show(char *o, int32_t c)  { mod_setting_show("display2020", "kits", "on", o, c); }
static void reskits_change(int32_t d)         { mod_setting_change("display2020", "kits", "on", NULL, 0, d); }
static void resband_show(char *o, int32_t c)  { mod_setting_show("display2020", "band", "smooth", o, c); }
static void resband_change(int32_t d)         { mod_setting_change("display2020", "band", "smooth", k_bands, 3, d); }
static void board_show(char *o, int32_t c)    { mod_setting_show("scoreboard", "corner", "top-left", o, c); }
static void board_change(int32_t d)           { mod_setting_change("scoreboard", "corner", "top-left", k_corners, 4, d); }


/* ===================================================================== RULES
 * The tweaks that change the original's rules (include/tweaks.h): the contest fix and SWOS 2020's
 * league rules, ON by default; the stronger keepers and the CPU's tactics (launcher options).
 * Every one is OFF LINE ONLY: an online match, and the test rigs, play the original. */

#if SWOS_TWEAK_FIXES
static void tweak_flip(uint32_t bit)
{
    O()->tweaks ^= bit;
    Tweaks_Set(O()->tweaks);
    save();
}
static void contest_show(char *o, int32_t c)
{
    snprintf(o, (size_t)c, "%s", (O()->tweaks & TWEAK_CONTEST_OWN_RATINGS) ? "FIXED" : "ORIGINAL");
}
static void contest_change(int32_t dir)
{
    (void)dir;
    tweak_flip(TWEAK_CONTEST_OWN_RATINGS);
    note((O()->tweaks & TWEAK_CONTEST_OWN_RATINGS) ? "EACH SIDE RATED BY ITS MAN AT THE BALL; OFF LINE ONLY"
                                                    : "THE ORIGINAL RATES ONE SIDE BY A TEAM-MATE");
}
static void league_tweak(uint32_t bit, const char *on, const char *off)
{
    extern void TeamDb_ApplyLeagueTweaks(void);
    tweak_flip(bit);
    TeamDb_ApplyLeagueTweaks();
    note((O()->tweaks & bit) ? on : off);
}
static void lfix_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", (O()->tweaks & TWEAK_LEAGUE_CRASH_FIXES) ? "ON" : "OFF"); }
static void lfix_change(int32_t dir)
{
    (void)dir;
    league_tweak(TWEAK_LEAGUE_CRASH_FIXES, "LEAGUES FITTED TO THEIR TEAMS: NEW CAREERS", "THE ORIGINAL'S: NEW CAREERS");
}
static void lrules_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", (O()->tweaks & TWEAK_LEAGUE_RULES_2020) ? "SWOS 2020" : "ORIGINAL"); }
static void lrules_change(int32_t dir)
{
    (void)dir;
    league_tweak(TWEAK_LEAGUE_RULES_2020, "GREECE, ISRAEL, ITALY, SCOTLAND: NEW CAREERS", "THE ORIGINAL'S: NEW CAREERS");
}
static void points_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", (O()->tweaks & TWEAK_THREE_POINTS) ? "3" : "ORIGINAL"); }
static void points_change(int32_t dir)
{
    (void)dir;
    league_tweak(TWEAK_THREE_POINTS, "3 POINTS FOR A WIN: NEW CAREERS", "THE ORIGINAL'S: NEW CAREERS");
}
static void subs_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", (O()->tweaks & TWEAK_FIVE_SUBSTITUTES) ? "5 FROM 5" : "ORIGINAL"); }
static void subs_change(int32_t dir)
{
    (void)dir;
    league_tweak(TWEAK_FIVE_SUBSTITUTES, "5 SUBSTITUTES FROM 5: NEW CAREERS", "THE ORIGINAL'S: NEW CAREERS");
}
#endif

static void exact_show(char *o, int32_t c) { snprintf(o, (size_t)c, "%s", O()->career_exact_continuation ? "ON" : "OFF"); }
static void exact_change(int32_t dir)
{
    extern void SaveJson_SetExactContinuation(int32_t on);
    (void)dir;
    O()->career_exact_continuation = !O()->career_exact_continuation;
    SaveJson_SetExactContinuation(O()->career_exact_continuation);
    save();
    note(O()->career_exact_continuation ? "A LOADED CAREER PLAYS ON EXACTLY AS IT WOULD HAVE"
                                        : "THE ORIGINAL'S: A LOADED CAREER PLAYS ON DIFFERENTLY");
}


/* ===================================================================== the pages, by topic
 * (2026-11-24: sorted by what a setting is about -- GAMEPLAY how a match plays, APPEARANCE how the
 * game and its menus look, RULES what competitions count and which kits clash, GENERAL the
 * replays, the start-up and the way into a match. KIT COLOURS is on both APPEARANCE and RULES, the
 * same setting; PIXEL FILTER is VIDEO's, beside SCALING. SWOS.ini's keys are as they were.) */

static const Row k_gameplayRows[] = {
    { "GAME STYLE",              style_show,    style_change },
#if SWOS_TWEAK_FIXES
    { "BALL CONTEST",            contest_show,  contest_change },
#endif
#if SWOS_TWEAK_STRONGER_GK
    { "STRONGER KEEPERS",        keepers_show,  keepers_change },
#endif
    { "CPU TACTICS",             tactics_show,  tactics_change },
};
static const Page k_gameplayPage = { "GAMEPLAY OPTIONS", k_gameplayRows, (int32_t)(sizeof k_gameplayRows / sizeof k_gameplayRows[0]) };
_Static_assert(sizeof k_gameplayRows / sizeof k_gameplayRows[0] <= kRows, "a settings page holds kRows rows at most");

static const Row k_displayRows[] = {
    { "GAME GRAPHICS",      art_show,      art_change },
    { "MENU BUTTON LOOK",   look_show,     look_change },     /* (the buttons' shading; MENU BACKGROUND below) */
    { "MENU BACKGROUND",    menubg_show,   menubg_change, 0, menubg_off },
    { "MENU LOGO",          logo_show,     logo_change },
    { "MENU CURSOR",        cursor_show,   cursor_change },
    { "MENU OPACITY",       opacity_show,  opacity_change },
    { "KIT COLOURS",        kitcols_show,  kitcols_change },    /* (RULES' too: which kits clash) */
    { "GK/REF CLASH KITS",  clashkits_show, clashkits_change },
    { "PITCH PALETTE",      pitchpal_show, pitchpal_change },
    { "KITS ON THE RESULT", reskits_show,  reskits_change },
    { "RESULT BAND",        resband_show,  resband_change },
    { "SCOREBOARD",         board_show,    board_change },
};
static const Page k_displayPage = { "APPEARANCE OPTIONS", k_displayRows, (int32_t)(sizeof k_displayRows / sizeof k_displayRows[0]) };
_Static_assert(sizeof k_displayRows / sizeof k_displayRows[0] <= kRows, "a settings page holds kRows rows at most");

static const Row k_rulesRows[] = {
    { "KIT COLOURS",             kitcols_show,  kitcols_change },   /* (which kits clash: online, both players'; DISPLAY's too) */
#if SWOS_TWEAK_FIXES
    { "LEAGUE RULES",            lrules_show,   lrules_change },
    { "POINTS FOR A WIN",        points_show,   points_change },
    { "SUBSTITUTES",             subs_show,     subs_change },
    { "LEAGUE CRASH FIXES",      lfix_show,     lfix_change },
#endif
    { "CAREER EXACT LOADS",      exact_show,    exact_change },
};
static const Page k_rulesPage = { "RULES OPTIONS", k_rulesRows, (int32_t)(sizeof k_rulesRows / sizeof k_rulesRows[0]) };
_Static_assert(sizeof k_rulesRows / sizeof k_rulesRows[0] <= kRows, "a settings page holds kRows rows at most");

static const Row k_generalRows[] = {
    { "AUTO SAVE REPLAYS",       autosave_show, autosave_change },
    { "HIGHLIGHT DEFAULT SPEED", hlspeed_show,  hlspeed_change },
    { "FRAME INFO OVERLAY",      overlay_show,  overlay_change },
    { "SKIP INTRO",              intro_show,    intro_change },
    { "INTRO PICTURES",          reels_show,    reels_change },
    { "PRE-MATCH PAUSE",         loadpause_show, loadpause_change },
    { "MOUSE POINTER",           pointer_show,  pointer_change },   /* (APPEARANCE's till 2026-11-25: MENU LOGO took its row) */
};
static const Page k_generalPage = { "GENERAL OPTIONS", k_generalRows, (int32_t)(sizeof k_generalRows / sizeof k_generalRows[0]) };
_Static_assert(sizeof k_generalRows / sizeof k_generalRows[0] <= kRows, "a settings page holds kRows rows at most");

/* ===================================================================== SETTINGS (the hub) */

static void page_show(const Page *page)
{
    uint8_t *menu = MenuJson_Load(SETTINGS_PAGE_NAME);
    if (!menu) {
        fprintf(stderr, "[settings] cannot show %s\n", SETTINGS_PAGE_NAME);
        return;
    }
    s_page = page;
    note("FIRE OR RIGHT CHANGES A SETTING, LEFT GOES BACK");
    ShowMenu(menu);
}

enum { Hub_header, Hub_subTitle, Hub_controls, Hub_video, Hub_audio, Hub_gameplay, Hub_note, Hub_exit };
static char s_hubNote[64];

static void settingsMenu_OnInit(void) { entry_at(Hub_note)->fg = (uintptr_t)s_hubNote; }
static void settingsMenu_OnDraw(void) { entry_at(Hub_note)->fg = (uintptr_t)s_hubNote; }
static void settingsMenu_controls_OnSelect(void) { page_show(&k_controlsPage); }
static void settingsMenu_video_OnSelect(void)    { page_show(&k_videoPage); }
static void settingsMenu_audio_OnSelect(void)    { page_show(&k_audioPage); }
static void settingsMenu_gameplay_OnSelect(void) { page_show(&k_gameplayPage); }
static void settingsMenu_display_OnSelect(void)  { page_show(&k_displayPage); }   /* (launcher options, agent B) */
static void settingsMenu_rules_OnSelect(void)    { page_show(&k_rulesPage); }     /* (the tweaks on by default) */
static void settingsMenu_general_OnSelect(void)  { page_show(&k_generalPage); }   /* (replays, start-up) */
/* A change made that needs the game started again: RESTART NOW / LATER, once per such change. 1
 * when the game is starting again (the caller leaves it at that), 0 to go on. */
int32_t Options_AskRestart(void)
{
    extern int32_t Main_CanRelaunch(void);
    extern int32_t Main_Relaunch(void);
    if (!s_restartPending || !Main_CanRelaunch())
        return 0;
    static const char *const kButtons[2] = { "RESTART NOW", "LATER" };
    s_restartPending = 0;               /* (asked once; a later change asks again) */
    fprintf(stderr, "[settings] a change needs a restart: asking\n");
    return MenuDialog_Show("RESTART", "SOME OF YOUR CHANGES TAKE EFFECT ONLY WHEN THE GAME STARTS AGAIN."
                           "|RESTART THE GAME NOW?", kButtons, 2, 0) == 0 && Main_Relaunch();
}

static void settingsMenu_exit_OnSelect(void)
{
    if (Options_AskRestart())
        return;                         /* (the game closes; the new one is starting) */
    SetExitMenuFlag();
}

/* ===================================================================== CONTROLLER SETUP */

enum {                                      /* joypadConfigMenu.json's entries */
    Jpc_header = 0,
    Jpc_name = 15, Jpc_guid, Jpc_id, Jpc_numButtons, Jpc_numAxes, Jpc_powerLevel, Jpc_rawX, Jpc_rawY,
    Jpc_fireButton, Jpc_benchButton, Jpc_deadZoneXPos, Jpc_deadZoneYPos, Jpc_deadZoneXNeg, Jpc_deadZoneYNeg,
    Jpc_decYNeg, Jpc_yNegPercentage, Jpc_incYNeg,
    Jpc_decYPos, Jpc_yPosPercentage, Jpc_incYPos,
    Jpc_incXNeg, Jpc_xNegPercentage, Jpc_decXNeg,
    Jpc_incXPos, Jpc_xPosPercentage, Jpc_decXPos,
    Jpc_exit,
    Jpc_stickAxesLabel, Jpc_stickAxes,      /* OpenSWOS's: STICK (GitHub issue 28) */
    Jpc_kNumEntries
};
/* joypadConfig.mnu's exports: the cut-off box. */
enum { kBoxX1 = 160, kBoxY1 = 60, kBoxX2 = 280, kBoxY2 = 166 };
/* InputSDL_PadCut's k: x+, x-, y+, y- */
enum { CUT_XP, CUT_XN, CUT_YP, CUT_YN };

static char s_jpcName[64], s_jpcGuid[40], s_jpcHeader[48];
static char s_jpcNum[5][16];            /* id, buttons, axes, raw x, raw y */
static const char *s_jpcPower = "";
static char s_jpcButtons[2][24], s_jpcCut[4][12], s_jpcPct[4][8];
static const char *s_jpcStick = "";
/* STICK's values, by InputSDL_PadStick's bits (1 flip X, 2 flip Y, 4 swap -- the swap first). */
static const char *const k_stickNames[8] = {
    "NORMAL", "FLIP L/R", "FLIP U/D", "FLIP ALL", "SWAP", "SWAP+L/R", "SWAP+U/D", "SWAP+ALL",
};
static int  s_jpcDirs[4];               /* up, down, left, right: what the stick reads */

static int cut_pct(int k)
{
    const int c = InputSDL_PadCut(s_setupDev, k);
    return c ? c : InputSDL_StickDeadzonePercent();
}

static void jpc_values(void)
{
    char guid[33];
    int id, buttons, axes, rawX, rawY, k;
    if (!InputSDL_DeviceOpen(s_setupDev))
        return;
    InputSDL_DeviceInfo(s_setupDev, guid, &id, &buttons, &axes, &s_jpcPower, &rawX, &rawY, s_jpcDirs);
    {
        char label[64];
        InputSDL_DeviceLabel(s_setupDev, label, sizeof label);
        upper(s_jpcName, sizeof s_jpcName, label);
    }
    upper(s_jpcGuid, sizeof s_jpcGuid, guid);
    snprintf(s_jpcNum[0], sizeof s_jpcNum[0], "%d", id);
    snprintf(s_jpcNum[1], sizeof s_jpcNum[1], "%d", buttons);
    snprintf(s_jpcNum[2], sizeof s_jpcNum[2], "%d", axes);
    snprintf(s_jpcNum[3], sizeof s_jpcNum[3], "%d", rawX);
    snprintf(s_jpcNum[4], sizeof s_jpcNum[4], "%d", rawY);
    for (k = 0; k < 2; k++) {
        char nm[24];
        InputSDL_PadButtonName(s_setupDev, k, InputSDL_PadButton(s_setupDev, k), nm, sizeof nm);
        upper(s_jpcButtons[k], sizeof s_jpcButtons[k], nm);
    }
    s_jpcStick = k_stickNames[InputSDL_PadStick(s_setupDev) & 7];
    for (k = 0; k < 4; k++) {
        const int pct = cut_pct(k);
        /* SWOS 2020 shows each cut-off as the axis value it is, and its percent in the box. */
        snprintf(s_jpcCut[k], sizeof s_jpcCut[k], "%d", (k == CUT_XN || k == CUT_YN ? -1 : 1) * 32767 * pct / 100);
        snprintf(s_jpcPct[k], sizeof s_jpcPct[k], "%d%%", pct);
    }
}

static void jpc_texts(void)
{
    static const int32_t cutEntry[4] = { Jpc_deadZoneXPos, Jpc_deadZoneXNeg, Jpc_deadZoneYPos, Jpc_deadZoneYNeg };
    static const int32_t pctEntry[4] = { Jpc_xPosPercentage, Jpc_xNegPercentage, Jpc_yPosPercentage, Jpc_yNegPercentage };
    int32_t k;
    entry_at(Jpc_header)->fg = (uintptr_t)(s_jpcHeader[0] ? s_jpcHeader : "CONTROLLER SETUP");
    entry_at(Jpc_name)->fg = (uintptr_t)s_jpcName;
    entry_at(Jpc_guid)->fg = (uintptr_t)s_jpcGuid;
    entry_at(Jpc_id)->fg = (uintptr_t)s_jpcNum[0];
    entry_at(Jpc_numButtons)->fg = (uintptr_t)s_jpcNum[1];
    entry_at(Jpc_numAxes)->fg = (uintptr_t)s_jpcNum[2];
    entry_at(Jpc_powerLevel)->fg = (uintptr_t)s_jpcPower;
    entry_at(Jpc_rawX)->fg = (uintptr_t)s_jpcNum[3];
    entry_at(Jpc_rawY)->fg = (uintptr_t)s_jpcNum[4];
    entry_at(Jpc_fireButton)->fg = (uintptr_t)s_jpcButtons[0];
    entry_at(Jpc_benchButton)->fg = (uintptr_t)s_jpcButtons[1];
    entry_at(Jpc_stickAxes)->fg = (uintptr_t)s_jpcStick;
    for (k = 0; k < 4; k++) {
        entry_at(cutEntry[k])->fg = (uintptr_t)s_jpcCut[k];
        entry_at(pctEntry[k])->fg = (uintptr_t)s_jpcPct[k];
    }
}

/* The box (SWOS 2020's drawDeadZoneBox): the stick's whole travel, its cut-offs as the inner
 * rectangle, and each quarter lit while the stick is in it -- outside the cut-offs (it counts)
 * or inside (it does not). In the menu's own colours: 14 green, 10 red, 13 / 3 blue, 1 the frame. */
static void fill(int x1, int y1, int x2, int y2, int colour)
{
    if (x2 > x1 && y2 > y1)
        DrawMenuItemBackground((uint16_t)colour, (uint16_t)x1, (uint16_t)y1, (uint16_t)(x2 - x1), (uint16_t)(y2 - y1));
}

static void jpc_box(void)
{
    const int cx = kBoxX1 + (kBoxX2 - kBoxX1 + 1) / 2, cy = kBoxY1 + (kBoxY2 - kBoxY1 + 1) / 2;
    const int up = s_jpcDirs[0], down = s_jpcDirs[1], left = s_jpcDirs[2], right = s_jpcDirs[3];
    const int any = up || down || left || right;
    const int x1 = cx - (cx - kBoxX1) * cut_pct(CUT_XN) / 100, x2 = cx + (kBoxX2 - cx) * cut_pct(CUT_XP) / 100;
    const int y1 = cy - (cy - kBoxY1) * cut_pct(CUT_YN) / 100, y2 = cy + (kBoxY2 - cy) * cut_pct(CUT_YP) / 100;
    int rawX = 0, rawY = 0;
    sscanf(s_jpcNum[3], "%d", &rawX);
    sscanf(s_jpcNum[4], "%d", &rawY);
    InputSDL_PadStickTurn(s_setupDev, &rawX, &rawY);     /* (where the game has it: STICK applied) */
    /* the quarters: lit green where the stick reads a direction, red where it sits in the cut-off */
    fill(kBoxX1, kBoxY1, cx, cy, (up || left) ? 14 : 13);
    fill(cx, kBoxY1, kBoxX2, cy, (up || right) ? 14 : 13);
    fill(kBoxX1, cy, cx, kBoxY2, (down || left) ? 14 : 13);
    fill(cx, cy, kBoxX2, kBoxY2, (down || right) ? 14 : 13);
    fill(x1, y1, x2, y2, !any && (rawX || rawY) ? 10 : 3);
    /* the frame and the cross */
    fill(kBoxX1, kBoxY1, kBoxX2 + 1, kBoxY1 + 1, 1);
    fill(kBoxX1, kBoxY2, kBoxX2 + 1, kBoxY2 + 1, 1);
    fill(kBoxX1, kBoxY1, kBoxX1 + 1, kBoxY2 + 1, 1);
    fill(kBoxX2, kBoxY1, kBoxX2 + 1, kBoxY2 + 1, 1);
    fill(cx, kBoxY1, cx + 1, kBoxY2, 1);
    fill(kBoxX1, cy, kBoxX2, cy + 1, 1);
    /* where the stick is */
    {
        const int px = cx + (int)((int64_t)rawX * (kBoxX2 - cx) / 32768);
        const int py = cy + (int)((int64_t)rawY * (kBoxY2 - cy) / 32768);
        fill(px - 1, py - 1, px + 2, py + 2, 2);
    }
}

static void joypadConfig_joypadConfigMenuOnInit(void)
{
    jpc_values();
    jpc_texts();
}

static void joypadConfig_joypadConfigMenuOnDraw(void)
{
    if (!InputSDL_DeviceOpen(s_setupDev)) {     /* (unplugged: SWOS 2020 leaves too) */
        SetExitMenuFlag();
        return;
    }
    jpc_values();
    jpc_texts();
    jpc_box();
}

/* FIRE / BENCH (SWOS 2020's selectButton): the next button pressed on this pad. ESC leaves it,
 * BACKSPACE gives back the default. */
static void jpc_select(int bench)
{
    int dev = -1, button = -1, done = 0;
    MenuEntry *h = entry_at(Jpc_header);
    snprintf(s_jpcHeader, sizeof s_jpcHeader, "PRESS THE %s BUTTON", bench ? "BENCH" : "FIRE");
    h->fg = (uintptr_t)s_jpcHeader;
    CallDrawMenuItem(h);
    InputSDL_PadPressBegin();
    InputSDL_CaptureKey(1);
    /* Waiting: the FIRE / BENCH value box yellow until a button is pressed, as SELECT JOYPAD's. */
    MenuEntry *waitBox = entry_at(bench ? Jpc_benchButton : Jpc_fireButton);
    const uint16_t waitBoxWas = waitBox->bgColor;
    waitBox->bgColor = kWaitColour;
    CallDrawMenuItem(waitBox);
    while (!done) {
        const int32_t key = InputSDL_CapturedKey();
        MenuCheckControls();
        if (!InputSDL_DeviceOpen(s_setupDev))
            break;
        if (key == SDL_SCANCODE_ESCAPE)
            break;
        if (key == SDL_SCANCODE_BACKSPACE || key == SDL_SCANCODE_DELETE) {
            InputSDL_SetPadButton(s_setupDev, bench, -1);
            done = 1;
        } else if (key != SDL_SCANCODE_UNKNOWN)
            InputSDL_CaptureKey(1);
        if (!done && InputSDL_PadPressPoll(s_setupDev, &dev, &button)) {
            if (InputSDL_SetPadButton(s_setupDev, bench, button) != 0) {
                snprintf(s_jpcHeader, sizeof s_jpcHeader, "NOT THAT ONE: AGAIN");
                CallDrawMenuItem(h);
                continue;
            }
            done = 1;
        }
        CallDrawMenuItem(h);
    }
    InputSDL_CaptureKey(0);
    waitBox->bgColor = waitBoxWas;
    jpc_values();                       /* (the button just set, shown in the box now) */
    CallDrawMenuItem(waitBox);
    fprintf(stderr, "[settings] CONTROLLER SETUP, the %s button: %s\n", bench ? "BENCH" : "FIRE",
            done ? (button >= 0 ? "taken" : "the default") : "left");
    s_jpcHeader[0] = '\0';
    CallDrawMenuItem(h);                /* (the prompt gone now, not once the button is let go) */
    release_wait(done && button >= 0 ? s_setupDev : -2, button);
    save();
}

static void joypadConfig_selectFireButton(void)  { jpc_select(0); }
static void joypadConfig_selectBenchButton(void) { jpc_select(1); }

/* A cut-off by `by` percent (the +/- boxes: 1; fire on the value: 5, round from 95 to 5). */
static void jpc_cut(int k, int by)
{
    int v = cut_pct(k) + by;
    if (by == 5 && v > 95)
        v = 5;
    v = v < 5 ? 5 : v > 95 ? 95 : v;
    InputSDL_SetPadCut(s_setupDev, k, v);
    save();
}

/* STICK: fire steps through the eight (NORMAL, the flips, then the same swapped). */
static void joypadConfig_selectStickAxes(void)
{
    InputSDL_SetPadStick(s_setupDev, (InputSDL_PadStick(s_setupDev) + 1) & 7);
    save();
}

static void joypadConfig_enterDeadZoneXPos(void) { jpc_cut(CUT_XP, 5); }
static void joypadConfig_enterDeadZoneXNeg(void) { jpc_cut(CUT_XN, 5); }
static void joypadConfig_enterDeadZoneYPos(void) { jpc_cut(CUT_YP, 5); }
static void joypadConfig_enterDeadZoneYNeg(void) { jpc_cut(CUT_YN, 5); }
static void joypadConfig_increaseXPos(void)      { jpc_cut(CUT_XP, 1); }
static void joypadConfig_decreaseXPos(void)      { jpc_cut(CUT_XP, -1); }
static void joypadConfig_increaseXNeg(void)      { jpc_cut(CUT_XN, 1); }
static void joypadConfig_decreaseXNeg(void)      { jpc_cut(CUT_XN, -1); }
static void joypadConfig_increaseYPos(void)      { jpc_cut(CUT_YP, 1); }
static void joypadConfig_decreaseYPos(void)      { jpc_cut(CUT_YP, -1); }
static void joypadConfig_increaseYNeg(void)      { jpc_cut(CUT_YN, 1); }
static void joypadConfig_decreaseYNeg(void)      { jpc_cut(CUT_YN, -1); }

/* ===================================================================== F6: quick save */

/* SWOS 2020's F6 (controls.cpp:2202): in a menu, with a game in progress, the SAVE of the main
 * menu without going back to it. SWOS 2020 does not offer it on the main menu (which has SAVE),
 * in a continue/abort question, or on PLAY MATCH once the match is played; here, neither on
 * PLAY MATCH at all (a fixture half set up), nor during the season's turn, a text field, a
 * dialog, or --career-golden's run. */
void SettingsMenus_MenuFrame(void)
{
    extern uint16_t _gameType;
    extern uint16_t _seasonUpdateInProgress;
    extern volatile uint16_t _gTextInputActive;
    extern uint8_t _saveFileTitle[];
    extern int32_t Menu_IsMainMenu(void);
    extern void PlayMatchMenuInit(void);
    extern void QuickSave_FromMenu(void);
    extern int32_t CareerAutopilot_Active(void);
    static int32_t s_busy;
    const Menu *menu = (const Menu *)_currentMenu;

    if (!InputSDL_TakeQuickSave() || s_busy)
        return;
    if (_gameType == 0 || !_saveFileTitle[0] || Menu_IsMainMenu() || _seasonUpdateInProgress ||
        _gTextInputActive || (menu && menu->onInit == (MenuVoidFn)PlayMatchMenuInit) || CareerAutopilot_Active()) {
        fprintf(stderr, "[quicksave] F6: not here\n");
        return;
    }
    fprintf(stderr, "[quicksave] F6: %s\n", (const char *)_saveFileTitle);
    s_busy = 1;
    QuickSave_FromMenu();
    s_busy = 0;
    _shortFire = 0;                 /* (the dialog's last press is not this menu's) */
    InputSDL_ClearKeyState();
}

/* ===================================================================== registration */

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }

static const MenuSymbol k_handlers[] = {
    HANDLER(settingsMenu_OnInit),
    HANDLER(settingsMenu_OnDraw),
    HANDLER(settingsMenu_controls_OnSelect),
    HANDLER(settingsMenu_video_OnSelect),
    HANDLER(settingsMenu_audio_OnSelect),
    HANDLER(settingsMenu_gameplay_OnSelect),
    HANDLER(settingsMenu_display_OnSelect),
    HANDLER(settingsMenu_rules_OnSelect),
    HANDLER(settingsMenu_general_OnSelect),
    HANDLER(settingsMenu_exit_OnSelect),
    HANDLER(settingsPage_OnInit),
    HANDLER(settingsPage_OnDraw),
    HANDLER(settingsPage_row0_OnSelect),
    HANDLER(settingsPage_row1_OnSelect),
    HANDLER(settingsPage_row2_OnSelect),
    HANDLER(settingsPage_row3_OnSelect),
    HANDLER(settingsPage_row4_OnSelect),
    HANDLER(settingsPage_row5_OnSelect),
    HANDLER(settingsPage_row6_OnSelect),
    HANDLER(settingsPage_row7_OnSelect),
    HANDLER(settingsPage_row8_OnSelect),
    HANDLER(settingsPage_row9_OnSelect),
    HANDLER(settingsPage_row10_OnSelect),
    HANDLER(settingsPage_row11_OnSelect),
    HANDLER(settingsPage_exit_OnSelect),
    HANDLER(joypadConfig_joypadConfigMenuOnInit),
    HANDLER(joypadConfig_joypadConfigMenuOnDraw),
    HANDLER(joypadConfig_selectFireButton),
    HANDLER(joypadConfig_selectBenchButton),
    HANDLER(joypadConfig_selectStickAxes),
    HANDLER(joypadConfig_enterDeadZoneXPos),
    HANDLER(joypadConfig_enterDeadZoneXNeg),
    HANDLER(joypadConfig_enterDeadZoneYPos),
    HANDLER(joypadConfig_enterDeadZoneYNeg),
    HANDLER(joypadConfig_increaseXPos),
    HANDLER(joypadConfig_decreaseXPos),
    HANDLER(joypadConfig_increaseXNeg),
    HANDLER(joypadConfig_decreaseXNeg),
    HANDLER(joypadConfig_increaseYPos),
    HANDLER(joypadConfig_decreaseYPos),
    HANDLER(joypadConfig_increaseYNeg),
    HANDLER(joypadConfig_decreaseYNeg),
};

#undef HANDLER

void SettingsMenus_Init(void)
{
    static int32_t registered;
    if (registered)
        return;
    registered = 1;
    MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
}

/* OPTIONS -> SETTINGS. */
void SettingsMenus_Show(void)
{
    uint8_t *menu;
    SettingsMenus_Init();
    menu = MenuJson_Load(SETTINGS_MENU_NAME);
    if (!menu) {
        fprintf(stderr, "[settings] cannot show %s\n", SETTINGS_MENU_NAME);
        return;
    }
    s_hubNote[0] = '\0';
    ShowMenu(menu);
}

/* DISPLAY OPTIONS, checked without a window (launcher options, agent B): the pixel filters'
 * answers and the improved pitch palette's green, each set and back. */
static int DisplayOptions_SelfTest(void)
{
    int fails = PixelFilter_SelfTest();
    {
        uint8_t was[768], now[768];
        for (int type = 0; type < 7; type++) {
            _gImprovedPitchPalette = 0;
            DosGfx_BuildPalette(type, was);
            _gImprovedPitchPalette = 1;
            DosGfx_BuildPalette(type, now);
            _gImprovedPitchPalette = 0;
            const int differ = memcmp(was, now, sizeof was) != 0;
            const int want = type == 2 || type == 3;            /* wet, soft */
            if (differ != want || (want && !(now[9 * 3 + 1] > was[9 * 3 + 1]))) {
                fails++;
                fprintf(stderr, "[settings-selftest] FAIL: improved pitch palette, pitch type %d (entry 9 green %d -> %d)\n",
                        type, was[9 * 3 + 1], now[9 * 3 + 1]);
            }
        }
    }
    return fails;
}

/* --settings-menus-selftest: every page's rows shown and changed both ways and back, with no
 * window or device, and the three menus loaded. Returns the number of failures. */
int SettingsMenus_SelfTest(void)
{
    static const Page *const pages[] = { &k_controlsPage, &k_videoPage, &k_audioPage, &k_gameplayPage,
                                         &k_displayPage, &k_rulesPage, &k_generalPage };
    int fails = 0;
    SettingsMenus_Init();
    for (size_t p = 0; p < sizeof pages / sizeof pages[0]; p++) {
        if (pages[p]->count > kRows) {
            fprintf(stderr, "[settings-selftest] FAIL: %s has %d rows, the page %d\n",
                    pages[p]->title, (int)pages[p]->count, kRows);
            fails++;
        }
        for (int32_t i = 0; i < pages[p]->count; i++) {
            char v[kValueLen];
            pages[p]->rows[i].show(v, kValueLen);
            if (!v[0] || strlen(v) > 21) {
                fprintf(stderr, "[settings-selftest] FAIL: %s / %s shows \"%s\"\n", pages[p]->title, pages[p]->rows[i].label, v);
                fails++;
            }
        }
    }
    {   /* the menu cursor: steady is one colour, flashing the table as it was */
        extern uint16_t _kColorTableShine[8];
        uint16_t was[8];
        memcpy(was, _kColorTableShine, sizeof was);
        MenuLook_SetCursor(1);
        for (int i = 1; i < 8; i++)
            if (_kColorTableShine[i] != _kColorTableShine[0]) { fails++; fprintf(stderr, "[settings-selftest] FAIL: steady cursor flashes\n"); break; }
        MenuLook_SetCursor(0);
        if (memcmp(was, _kColorTableShine, sizeof was)) { fails++; fprintf(stderr, "[settings-selftest] FAIL: flashing cursor not restored\n"); }
    }
    fails += DisplayOptions_SelfTest();     /* (launcher options, agent B) */
    {   /* the three menus load (their entry count is checked by --menu-selftest) */
        static const char *const names[] = { SETTINGS_MENU_NAME, SETTINGS_PAGE_NAME, JOYPAD_CONFIG_NAME };
        for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
            if (!MenuJson_Load(names[i])) { fails++; fprintf(stderr, "[settings-selftest] FAIL: %s does not load\n", names[i]); }
    }
    fprintf(stderr, "[settings-selftest] %s\n", fails ? "FAILED" : "ok");
    return fails;
}

#endif /* SWOS_SDL_BUILD */

#ifndef SWOS_SDL_BUILD
typedef int settings_menus_amiga_has_none;     /* (ISO C: a translation unit is not empty) */
#endif
