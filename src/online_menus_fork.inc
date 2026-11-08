/*
 * online_menus_fork.inc -- SWOS United's own online screens: SWOS 2020's MATCH MAKING table, its
 * lobby (LOCATIONS, the stars, GO's tick and cross, TEAM DB), CHOOSE GAMEPLAY's buttons, the join
 * and accept sounds, "USING <pad>", the Discord match's start and end (Playaveli's, and United's
 * own). OpenSWOS's src/online_menus.c includes this file at its END when it exists (OpenSWOS has
 * none) and asks online_menus_fork() for these hooks (its OnlineMenusFork table) -- so a merge of
 * OpenSWOS never meets United's screens. Same translation unit: this code sees that file's helpers.
 */
#include "live_ticker.h"        /* the match on Discord (the discord2020 mod) */
#include "swos_io.h"            /* swos_data_path: the online sounds */
#include "kit_art.h"            /* KitArt_Return */
#include "mod_api.h"            /* ModApi_SettingInt: [mod.menus2020] accept_sound */

extern void United_DrawStyleIcon(int32_t icon, int32_t x, int32_t y);   /* (united_ui.h) */

#ifndef PROJECT_SWOS2020_MATCHMAKING
#define PROJECT_SWOS2020_MATCHMAKING 0
#endif

/* Which server to talk to. There is no account service -- the matchmaker has no
 * notion of a password, it takes a name and hands out introductions -- so LOGIN
 * connects with the username typed above and the password is not sent anywhere.
 *
 * Default is a matchmaker on this machine (scripts/run_matchmaker.sh), which is
 * what the whole local setup exists for. SWOS_MM_HOST / SWOS_MM_PORT point it
 * somewhere else without a rebuild. */
/* SWOS 2020's two online sounds (bin/sound_dos), through the one-shot sample. */
static void online_sound(const char *rel)
{
    extern int  AudioSDL_LoadOverlaySample(const char *path);
    extern void AudioSDL_PlayOverlaySample(void);
    char path[512];
    if (AudioSDL_LoadOverlaySample(swos_data_path(rel, path, sizeof path)) == 0) {
        AudioSDL_PlayOverlaySample();
        fprintf(stderr, "[mm] played %s\n", rel);
    }
}

/* ACCEPT SOUND (MULTIPLAYER SETTINGS): [mod.menus2020] accept_sound, on by default (was
 * [netplay] accept_sound). */
static int32_t accept_sound_on(void) { return ModApi_SettingInt("menus2020", "accept_sound", 1); }

static void mmsAcceptSound(void)
{
    ModApi_SetSetting("menus2020", "accept_sound", accept_sound_on() ? "off" : "on");
}

/* ======================================================================
 * MATCH MAKING as SWOS 2020 draws it (PROJECT_SWOS2020_MATCHMAKING: SWOS United)
 * ======================================================================
 * swos-2020's matchMakingMenu_OnDraw_New / setMatchMakingMenu_New / matchMaking_*_New
 * (src/mpgame.cpp, NEW_ACCEPT_MENU): row 0 of the table is its header (OPEN GAMES (n),
 * MODE, ACTION), the seven rows under it are always drawn ("-" when empty), a page of
 * games or of players turns with the red arrows beside each list, and a host with
 * players asking to join sees them in the table, each with ACCEPT -- no prompt. The
 * menu's five states are SWOS 2020's menuMode: not connected, the lobby, hosting
 * (waiting), asked to join someone's game, hosting with players asking.
 *
 * MODE: the matchmaker keeps no data with a game (Server/User.cpp: the blob is accepted
 * and dropped), so only this player's own game says what it is; another's shows "-". */
#ifndef PROJECT_SWOS2020_MATCHMAKING
#define PROJECT_SWOS2020_MATCHMAKING 0
#endif
#if PROJECT_SWOS2020_MATCHMAKING
#include "text_render.h"        /* GetTextWidth: a name too wide for its column */

static int32_t s_openMode;          /* (defined with the lobby below: what OPEN's game is for) */

enum { MM_ROWS = MatchMakingMenu_kGS_MaxRows - 1, MM_USERS = MatchMakingMenu_kGS_MaxRows2,
       kMmWhiteText = 2, kMmYellowText = 15, kTextLeft = 1 << 15 };
typedef enum { MMM_X, MMM_0_DEFAULT, MMM_1_WAIT, MMM_2_REQUESTED, MMM_3_ACCEPT } MmMenuMode;

static int32_t s_gamesPage, s_usersPage, s_joinersPage;
static char    s_mm2Host[MM_ROWS][MM_NAME_MAX + 8], s_mm2Mode[MM_ROWS][24], s_mm2Action[MM_ROWS][12];
static char    s_mm2User[MM_USERS][MM_NAME_MAX + 4];
static char    s_mm2Head[40], s_mm2Page[24], s_mm2Name[MM_NAME_MAX + 8], s_mm2Players[24];

static int32_t is_me(const char *name)
{
    return name && SDL_strcasecmp(name, MmClient_UserName()[0] ? MmClient_UserName() : s_username) == 0;
}

/* This player's own game is in the open list. */
static int32_t mm2_hosting_listed(void)
{
    int32_t i, n = 0;
    const MmGame *games = MmClient_OpenGames(&n);
    for (i = 0; i < n; i++)
        if (is_me(games[i].owner))
            return 1;
    return 0;
}

static MmMenuMode mm2_mode(void)
{
    const MmGameInfo *gi;
    if (MmClient_State() != MM_CONNECTED)
        return MMM_X;
    gi = MmClient_GameInfo();
    if (!gi)
        return MMM_0_DEFAULT;
    if (is_me(gi->owner) || (!gi->owner[0] && mm2_hosting_listed()))   /* (the owner can come a poll late) */
        return gi->numRequested > 0 ? MMM_3_ACCEPT : MMM_1_WAIT;
    return MMM_2_REQUESTED;
}

static int32_t mm2_pages(int32_t n, int32_t per) { return n > 0 ? (n + per - 1) / per : 1; }

/* The players asking to join this host's game that may (Account_JoinAllowed): their indices in
 * gi->requested. Anyone else is refused, once -- a request can arrive in the same poll as
 * another, past the one-shot event. */
static int32_t mm2_allowed(const MmGameInfo *gi, int32_t *idx)
{
    static char refused[8][MM_NAME_MAX];
    static int32_t numRefused;
    int32_t i, j, n = 0;
    for (i = 0; gi && i < gi->numRequested; i++) {
        const char *name = gi->requested[i].name;
        if (Account_JoinAllowed(name)) {
            idx[n++] = i;
            continue;
        }
        for (j = 0; j < numRefused; j++)
            if (strcmp(refused[j], name) == 0)
                break;
        if (j == numRefused) {
            snprintf(refused[numRefused % 8], sizeof refused[0], "%s", name);
            numRefused = numRefused < 8 ? numRefused + 1 : 8;
            fprintf(stderr, "[mm] refusing '%s': not this match's opponent\n", name);
            MmClient_Eject(name);
        }
    }
    return n;
}

/* The game this player asked to join, while waiting: told apart when it ends, the host's game
 * still open (refused: SWOS 2020's YOU ARE NOT ALLOWED TO JOIN) or gone (THE HOST LEFT). */
static char s_mm2AskedOwner[MM_NAME_MAX];

static int32_t mm2_still_open(const char *owner)
{
    int32_t i, n = 0;
    const MmGame *games = MmClient_OpenGames(&n);
    for (i = 0; owner[0] && i < n; i++)
        if (SDL_strcasecmp(games[i].owner, owner) == 0)
            return 1;
    return 0;
}

/* The text colour and alignment of an entry (SWOS 2020's e->textColor = colour | flags). */
static void text_style(int32_t ordinal, uint16_t color, uint16_t align)
{
    MenuEntry *e = entry_at(ordinal);
    e->stringFlags = (uint16_t)((e->stringFlags & ~(STR_COLOR_MASK | (1u << 15) | (1u << 14))) | color | align);
}

/* SWOS 2020's shortenName / shortenNameScen: the small font's glyphs added up, and the one
 * that goes past the limit becomes a '.' and ends the name. */
static void shorten_dot(char *s, int32_t limit)
{
    extern uint8_t _smallCharsTable[];
    const int32_t spacing = ((CharsTableHeader *)_smallCharsTable)->charSpacing;
    int32_t i, w = 0;
    for (i = 0; s[i]; i++) {
        uint8_t one[2] = { (uint8_t)s[i], 0 };
        w += GetTextWidth(one, _smallCharsTable) - spacing;
        if (w > limit) {
            s[i] = '.';
            s[i + 1] = 0;
            break;
        }
    }
}

static void upper_in_place(char *s)
{
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z')
            *s = (char)(*s - 'a' + 'A');
}

/* This player's own game: what OPEN made it for, in which style (getMatchMakingModeStr). */
static const char *own_mode(void)
{
    extern int _gDosGameplay;
    static char text[24];
    snprintf(text, sizeof text, "%s (%s)",
             s_openMode == LOBBY_MODE_RANKED ? "RANKED" : s_openMode == LOBBY_MODE_LEAGUE ? "TOURNEY" : "FRIENDLY",
             _gDosGameplay ? "DOS" : "AMIGA");
    return text;
}

/* SWOS 2020's drawScrollButtons: the red up and down arrows beside each list, each drawn by
 * its own entry after it is drawn (a redraw of one leaves it there). */
static const char *const k_mm2Arrows[12] = {
    "..A...........A.........",
    "..A3..........A3........",
    ".AAA..........A3........",
    ".AAA3.........A3........",
    "AAAAA.........A3........",
    "A.A3A3......A.A3A.......",
    "..A3.3......AAAAA3......",
    "..A3.........AAA33......",
    "..A3.........AAA3.......",
    "..A3..........A33.......",
    "..A3..........A3........",
    "...3...........3........",
};

static void mm2_arrow_AfterDraw(MenuEntry *e)
{
    extern volatile uintptr_t _frontbufferBasePtr;
    extern int _menu_draw_x_offset;
    uint8_t *fb = (uint8_t *)_frontbufferBasePtr + _menu_draw_x_offset;
    const int32_t down = e == entry_at(MatchMakingMenu_scrollDown) || e == entry_at(MatchMakingMenu_scrollDown2);
    const int32_t users = e == entry_at(MatchMakingMenu_scrollUp2) || e == entry_at(MatchMakingMenu_scrollDown2);
    const int32_t ox = e->x + (users ? 2 : 1), oy = e->y + 1;
    int32_t x, y;
    for (y = 0; y < 12; y++)
        for (x = 0; x < 12; x++) {
            const char c = k_mm2Arrows[y][down * 12 + x];
            if (c != '.')
                fb[(uint32_t)(oy + y) * FRONTBUFFER_ROW_STRIDE_BYTES + ox + x] =
                    (uint8_t)(c <= '9' ? c - '0' : c - 'A' + 10);
        }
}

static void link(int32_t ordinal, int32_t left, int32_t right, int32_t up, int32_t down)
{
    MenuEntry *e = entry_at(ordinal);
    if (left != -2)  e->leftEntry  = (int8_t)left;
    if (right != -2) e->rightEntry = (int8_t)right;
    if (up != -2)    e->upEntry    = (int8_t)up;
    if (down != -2)  e->downEntry  = (int8_t)down;
    e->directionLeft = e->directionRight = e->directionUp = e->directionDown = -1;
}

/* SWOS 2020's menu navigation (matchMakingMenu_OnDraw_New): ACTION is the column the cursor
 * walks, the arrows beside it, the player list right of them. -2: the template's link kept. */
static void mm2_links(void)
{
    int32_t r;
    for (r = 1; r <= MM_ROWS; r++)
        link(MatchMakingMenu_action_00 + r, -2, r <= 3 ? MatchMakingMenu_scrollUp : MatchMakingMenu_scrollDown,
             r > 1 ? MatchMakingMenu_action_00 + r - 1 : -2,
             r < MM_ROWS ? MatchMakingMenu_action_00 + r + 1 : MatchMakingMenu_openGame);
    link(MatchMakingMenu_scrollUp, MatchMakingMenu_action_00 + 1, MatchMakingMenu_users_00, -2, MatchMakingMenu_scrollDown);
    link(MatchMakingMenu_scrollDown, MatchMakingMenu_action_00 + MM_ROWS, MatchMakingMenu_users_00 + MM_USERS - 1,
         MatchMakingMenu_scrollUp, -2);
    link(MatchMakingMenu_scrollUp2, MatchMakingMenu_users_00, -2, -2, MatchMakingMenu_scrollDown2);
    link(MatchMakingMenu_scrollDown2, MatchMakingMenu_users_00 + MM_USERS - 1, -2, MatchMakingMenu_scrollUp2, -2);
    for (r = 0; r < MM_USERS; r++)
        link(MatchMakingMenu_users_00 + r, r < 5 ? MatchMakingMenu_scrollUp : MatchMakingMenu_scrollDown,
             r < 5 ? MatchMakingMenu_scrollUp2 : MatchMakingMenu_scrollDown2,
             r > 0 ? MatchMakingMenu_users_00 + r - 1 : -1, r < MM_USERS - 1 ? MatchMakingMenu_users_00 + r + 1 : -1);
    /* the buttons' top row goes up into the table, the rows under it up to the button above
     * (SWOS 2020 sent CANCEL up into the table too: from EXIT, up skipped HOST MATCH) */
    link(MatchMakingMenu_menuSettings, -2, -2, MatchMakingMenu_action_00 + MM_ROWS, -2);
    link(MatchMakingMenu_openGame, -2, -2, MatchMakingMenu_action_00 + MM_ROWS, -2);
    link(MatchMakingMenu_rankings, -2, -2, MatchMakingMenu_action_00 + MM_ROWS, -2);
    link(MatchMakingMenu_cancelGame, -2, -2, MatchMakingMenu_openGame, -2);
    for (r = 0; r <= MM_ROWS; r++) {            /* (the name and mode cells are not stops) */
        entry_at(MatchMakingMenu_host_00 + r)->isDisabled = 1;
        entry_at(MatchMakingMenu_mode_00 + r)->isDisabled = 1;
    }
    entry_at(MatchMakingMenu_action_00)->isDisabled = 1;
}

/* setMatchMakingMenu_New: the line of help and the nine buttons, by the menu's state. */
static void mm2_buttons(MmMenuMode mode)
{
    static const char *const kDesc[5] = {
        "SERVER NOT CONNECTED", "HOST OR JOIN A MATCH", "WAITING FOR OTHERS TO JOIN",
        "WAITING FOR HOST TO RESPOND", "ACCEPT A JOINER OR LEAVE",
    };
    const int32_t busy = mode == MMM_1_WAIT || mode == MMM_2_REQUESTED || mode == MMM_3_ACCEPT;
    set_entry(MatchMakingMenu_desc, mode == MMM_X && MmClient_State() == MM_CONNECTING ? "CONNECTING TO SERVER"
                                                                                       : kDesc[mode], 0, 0);
    if (mode == MMM_0_DEFAULT) {        /* (the cursor on a name: SWOS 2020's hint) */
        extern uint8_t _currentMenu[];
        const MenuEntry *sel = ((Menu *)_currentMenu)->selectedEntry;
        if (sel >= entry_at(MatchMakingMenu_users_00) && sel <= entry_at(MatchMakingMenu_users_00 + MM_USERS - 1))
            set_entry(MatchMakingMenu_desc, "CLICK PLAYER NAME TO VIEW MATCH INFO", 0, 0);
    }
    set_entry(MatchMakingMenu_openGame, "HOST MATCH", 1, mode == MMM_0_DEFAULT ? kGreen : kGray);
    set_entry(MatchMakingMenu_cancelGame, "CANCEL", 1, busy ? kGreen : kGray);
    set_entry(MatchMakingMenu_menuSettings, "SETTINGS", 1, busy ? kGray : kGreen);
    set_entry(MatchMakingMenu_menuCustomTeam, "MY CUSTOM TEAM", 1, busy ? kGray : kGreen);
    set_entry(MatchMakingMenu_rankings, "RANKINGS", 1, busy ? kGray : kPurple);
    set_entry(MatchMakingMenu_leagues, "LEAGUES", 1, busy ? kGray : kPurple);
    set_entry(MatchMakingMenu_matchLogs, "MATCH LOGS", 1, busy ? kGray : kPurple);
    set_entry(MatchMakingMenu_menuExit, "EXIT", 1, kRed);
    /* RANKINGS, LEAGUES, MATCH LOGS: only a service with them (online_account.h). */
    entry_at(MatchMakingMenu_rankings)->isInvisible  = Account_Has(ACCOUNT_HAS_RANKINGS) ? 0 : 1;
    entry_at(MatchMakingMenu_leagues)->isInvisible   = Account_Has(ACCOUNT_HAS_LEAGUES) ? 0 : 1;
    entry_at(MatchMakingMenu_matchLogs)->isInvisible = Account_Has(ACCOUNT_HAS_MATCH_LOGS) ? 0 : 1;
}

static void mm2_draw(void)
{
    const MmMenuMode mode = mm2_mode();
    const MmGameInfo *gi = MmClient_GameInfo();
    int32_t numUsers = 0, numGames = 0, r;
    const MmUser *users = MmClient_Users(&numUsers);
    const MmGame *games = MmClient_OpenGames(&numGames);
    int32_t allowed[MM_MAX_IN_GAME];
    const int32_t numAllowed = mode == MMM_3_ACCEPT ? mm2_allowed(gi, allowed) : 0;
    const int32_t accept = mode == MMM_3_ACCEPT && numAllowed > 0;
    const int32_t numRows = accept ? numAllowed : numGames;
    int32_t *page = accept ? &s_joinersPage : &s_gamesPage;

    if (mode != MMM_X) {
        numUsers = numUsers < 0 ? 0 : numUsers;
    } else {
        numUsers = numGames = 0;
    }
    if (*page >= mm2_pages(numRows, MM_ROWS))
        *page = 0;
    if (s_usersPage >= mm2_pages(numUsers, MM_USERS))
        s_usersPage = 0;

    /* the lines above the table */
    set_text(MatchMakingMenu_connectionStatus, mode == MMM_X ? "SERVER : DISCONNECTED" : "SERVER : CONNECTED");
    text_style(MatchMakingMenu_connectionStatus, mode == MMM_X ? kMmWhiteText : kMmYellowText, kTextLeft);
    snprintf(s_mm2Players, sizeof s_mm2Players, "%d PLAYER%s", (int)numUsers, numUsers == 1 ? "" : "S");
    set_text(MatchMakingMenu_numOfUsers, s_mm2Players);
    entry_at(MatchMakingMenu_numOfUsers)->dwWidth = 120 - 8;    /* (its last letter off a 336-wide screen) */
    snprintf(s_mm2Name, sizeof s_mm2Name, "- %s -", s_username);
    upper_in_place(s_mm2Name);
    set_text(MatchMakingMenu_userName, s_mm2Name);

    /* the header row */
    if (accept)
        snprintf(s_mm2Head, sizeof s_mm2Head, "JOINING PLAYER(S) (%d)", (int)numAllowed);
    else
        snprintf(s_mm2Head, sizeof s_mm2Head, "OPEN GAMES (%d)", (int)numGames);
    set_entry(MatchMakingMenu_host_00, s_mm2Head, 0, kGray);
    set_entry(MatchMakingMenu_mode_00, accept ? "-" : "MODE", 0, kGray);
    set_entry(MatchMakingMenu_action_00, "ACTION", 0, kGray);

    /* the seven rows */
    for (r = 0; r < MM_ROWS; r++) {
        const int32_t idx = r + *page * MM_ROWS;
        const char *name = NULL;
        uint16_t actionColor = kLightBlue;
        snprintf(s_mm2Host[r], sizeof s_mm2Host[r], "-");
        snprintf(s_mm2Mode[r], sizeof s_mm2Mode[r], "-");
        snprintf(s_mm2Action[r], sizeof s_mm2Action[r], "-");
        if (accept && idx < numAllowed) {
            name = gi->requested[allowed[idx]].name;
            snprintf(s_mm2Host[r], sizeof s_mm2Host[r], "%s", name);
            snprintf(s_mm2Action[r], sizeof s_mm2Action[r], "ACCEPT");
            actionColor = kGreen;
        } else if (!accept && idx < numGames) {
            const int32_t mine = is_me(games[idx].owner);
            name = games[idx].owner;
            snprintf(s_mm2Host[r], sizeof s_mm2Host[r], mine ? "%s (YOU)" : "%s", name);
            if (mine)
                snprintf(s_mm2Mode[r], sizeof s_mm2Mode[r], "%s", own_mode());
            if (mode == MMM_0_DEFAULT && !mine) {
                snprintf(s_mm2Action[r], sizeof s_mm2Action[r], "JOIN");
                actionColor = kGreen;
            } else if (mode == MMM_2_REQUESTED && gi && SDL_strcasecmp(gi->owner, name) == 0) {
                snprintf(s_mm2Action[r], sizeof s_mm2Action[r], "REQUESTED");
            }
        }
        upper_in_place(s_mm2Host[r]);
        shorten_dot(s_mm2Host[r], 96 - 2);
        set_text(MatchMakingMenu_host_00 + 1 + r, s_mm2Host[r]);
        set_text(MatchMakingMenu_mode_00 + 1 + r, s_mm2Mode[r]);
        set_entry(MatchMakingMenu_action_00 + 1 + r, s_mm2Action[r], 1, actionColor);
        set_visible(MatchMakingMenu_host_00 + 1 + r, 1);
        set_visible(MatchMakingMenu_mode_00 + 1 + r, 1);
        set_visible(MatchMakingMenu_action_00 + 1 + r, 1);
    }
    set_visible(MatchMakingMenu_host_00, 1);
    set_visible(MatchMakingMenu_mode_00, 1);
    set_visible(MatchMakingMenu_action_00, 1);

    /* who is on */
    for (r = 0; r < MM_USERS; r++) {
        const int32_t idx = r + s_usersPage * MM_USERS;
        snprintf(s_mm2User[r], sizeof s_mm2User[r], "%s", idx < numUsers ? users[idx].name : "-");
        upper_in_place(s_mm2User[r]);
        shorten_dot(s_mm2User[r], 60);
        set_text(MatchMakingMenu_users_00 + r, s_mm2User[r]);
        text_style(MatchMakingMenu_users_00 + r, kMmWhiteText, kTextLeft);
        set_visible(MatchMakingMenu_users_00 + r, 1);
    }

    /* the page, when there is more than one */
    if (numRows > MM_ROWS)
        snprintf(s_mm2Page, sizeof s_mm2Page, "(%d OF %d)", (int)*page + 1, (int)mm2_pages(numRows, MM_ROWS));
    else
        s_mm2Page[0] = 0;
    set_text(MatchMakingMenu_page, s_mm2Page);

    entry_at(MatchMakingMenu_scrollUp)->afterDrawCb = entry_at(MatchMakingMenu_scrollDown)->afterDrawCb =
        entry_at(MatchMakingMenu_scrollUp2)->afterDrawCb = entry_at(MatchMakingMenu_scrollDown2)->afterDrawCb =
            mm2_arrow_AfterDraw;
    if (mode == MMM_2_REQUESTED && gi)
        snprintf(s_mm2AskedOwner, sizeof s_mm2AskedOwner, "%s", gi->owner);
    mm2_links();
    mm2_buttons(mode == MMM_3_ACCEPT && !accept ? MMM_1_WAIT : mode);
}

/* ACTION: JOIN a game, or (a host with players asking) ACCEPT one of them. */
static void matchMaking_action_onSelect(void)
{
    extern uint8_t _currentMenu[];
    const MmMenuMode mode = mm2_mode();
    const MmGameInfo *gi = MmClient_GameInfo();
    const MenuEntry *sel = ((Menu *)_currentMenu)->selectedEntry;
    int32_t r, numGames = 0;
    const MmGame *games = MmClient_OpenGames(&numGames);
    for (r = 1; r <= MM_ROWS; r++)
        if (sel == entry_at(MatchMakingMenu_action_00 + r))
            break;
    if (r > MM_ROWS)
        return;
    r--;
    if (mode == MMM_3_ACCEPT) {
        int32_t allowed[MM_MAX_IN_GAME];
        const int32_t n = mm2_allowed(gi, allowed), idx = r + s_joinersPage * MM_ROWS;
        if (idx < n)
            MmClient_Approve(gi->requested[allowed[idx]].name);
    } else if (mode == MMM_0_DEFAULT) {
        const int32_t idx = r + s_gamesPage * MM_ROWS;
        if (idx < numGames && !is_me(games[idx].owner)) {
            snprintf(s_mm2AskedOwner, sizeof s_mm2AskedOwner, "%s", games[idx].owner);
            MmClient_JoinGame(games[idx].owner);
        }
    }
}

static void mm2_turn(int32_t *page, int32_t n, int32_t per, int32_t dir)
{
    const int32_t total = mm2_pages(n, per);
    if (n <= 0)
        return;
    *page = (*page + dir + total) % total;
}

static void matchMaking_scrollUp_onSelect(void)
{
    const MmGameInfo *gi = MmClient_GameInfo();
    int32_t n = 0;
    MmClient_OpenGames(&n);
    int32_t allowed[MM_MAX_IN_GAME];
    if (mm2_mode() == MMM_3_ACCEPT)
        mm2_turn(&s_joinersPage, mm2_allowed(gi, allowed), MM_ROWS, -1);
    else
        mm2_turn(&s_gamesPage, n, MM_ROWS, -1);
}

static void matchMaking_scrollDown_onSelect(void)
{
    const MmGameInfo *gi = MmClient_GameInfo();
    int32_t n = 0;
    MmClient_OpenGames(&n);
    int32_t allowed[MM_MAX_IN_GAME];
    if (mm2_mode() == MMM_3_ACCEPT)
        mm2_turn(&s_joinersPage, mm2_allowed(gi, allowed), MM_ROWS, +1);
    else
        mm2_turn(&s_gamesPage, n, MM_ROWS, +1);
}

static void matchMaking_scrollUp2_onSelect(void)
{
    int32_t n = 0;
    MmClient_Users(&n);
    mm2_turn(&s_usersPage, n, MM_USERS, -1);
}

static void matchMaking_scrollDown2_onSelect(void)
{
    int32_t n = 0;
    MmClient_Users(&n);
    mm2_turn(&s_usersPage, n, MM_USERS, +1);
}
#endif /* PROJECT_SWOS2020_MATCHMAKING */

#if PROJECT_SWOS2020_MATCHMAKING
/* ---- SWOS 2020's lobby, as SWOS United draws it ----
 * (swos-2020's mpGameSetupMenu_OnDraw / drawGoButtonAndStarsGraphics, game.cpp): LOCATIONS -- a
 * flag and the ISO2 code -- the stars of each player's SRN right of their name, TEAMS and TACTICS
 * light brown, GO a red box with a tick (ready) or a cross, the TEAM DB line and row, the red
 * chat arrows. The country and SRN are the account's (Account_PlayerInfo: the site's nation or
 * flag code), never sent between the games: no change to what the two of them say. */
#include "swos2020_lobby_art.h"
#include "world.h"            /* World_Tag: TEAM DB */
#include "kit_art.h"          /* the little players in each team's kits */
#include "team_colors.h"      /* KIT_COLOURS */
#include "text_render.h"      /* GetTextWidth: a name clear of the stars */

extern void Platform_PaletteRgb(int index, uint8_t *r, uint8_t *g, uint8_t *b);

static char    s_locText[2][12], s_teamDb[24], s_teamDb1[12];
static int16_t s_locFlag[2] = { -1, -1 };
static int8_t  s_stars[2] = { -1, -1 }, s_goState[2] = { -1, -1 };

/* The site's country (an ISO2 code, a home nation's GB-ENG, or a name) as an ISO2 code. */
static const char *country_iso2(const char *c)
{
    static const char *const alias[][2] = {
        { "UNITED STATES", "US" }, { "USA", "US" }, { "UNITED KINGDOM", "GB" }, { "GREAT BRITAIN", "GB" },
        { "TURKIYE", "TR" }, { "KOREA, SOUTH", "KR" }, { "SOUTH KOREA", "KR" }, { "RUSSIAN FEDERATION", "RU" },
        { "CZECH REPUBLIC", "CZ" }, { "CZECHIA", "CZ" }, { "NETHERLANDS", "NL" }, { "HOLLAND", "NL" },
        { "WALES", "GB-WLS" }, { "SCOTLAND", "GB-SCT" }, { "ENGLAND", "GB-ENG" }, { "NORTHERN IRELAND", "GB-NIR" },
        { "BOSNIA AND HERZEGOVINA", "BA" }, { "IRAN", "IR" }, { "MACEDONIA", "MK" }, { "NORTH MACEDONIA", "MK" },
    };
    static char up[40];
    uint32_t i;
    for (i = 0; c[i] && i < sizeof up - 1; i++)
        up[i] = (char)(c[i] >= 'a' && c[i] <= 'z' ? c[i] - 'a' + 'A' : c[i]);
    up[i] = 0;
    if (strlen(up) == 2 || strncmp(up, "GB-", 3) == 0)
        return up;
    for (i = 0; i < sizeof alias / sizeof alias[0]; i++)
        if (strcmp(up, alias[i][0]) == 0)
            return alias[i][1];
    for (i = 0; i < sizeof k_countryNames / sizeof k_countryNames[0]; i++)
        if (strcmp(up, k_countryNames[i][0]) == 0)
            return k_countryNames[i][1];
    return NULL;
}

static int16_t flag_of(const char *iso2)
{
    uint32_t i;
    for (i = 0; iso2 && i < sizeof k_countryFlags / sizeof k_countryFlags[0]; i++)
        if (strcmp(iso2, k_countryFlags[i].code) == 0)
            return k_countryFlags[i].flag;
    return 0;                                   /* (the atlas's "unknown") */
}

/* SWOS 2020's _getStarLevel: ten levels of SRN; unranked counts as 5. */
static int8_t star_level(int32_t srn)
{
    static const int16_t from[10] = { -1000, 200, 376, 500, 700, 951, 1051, 1400, 1900, 2300 };
    int8_t level = -1, i;
    if (srn == -5000)
        return 5;
    for (i = 0; i < 10; i++)
        if (srn >= from[i])
            level = i;
    return level;
}

static uint8_t *lobby_fb(void)
{
    extern volatile uintptr_t _frontbufferBasePtr;
    extern int _menu_draw_x_offset;
    return (uint8_t *)_frontbufferBasePtr + _menu_draw_x_offset;
}

static void put_map(const char *const *rows, int32_t nrows, int32_t sx, int32_t w, int32_t ox, int32_t oy)
{
    uint8_t *fb = lobby_fb();
    int32_t x, y;
    for (y = 0; y < nrows; y++)
        for (x = 0; x < w; x++) {
            const char c = rows[y][sx + x];
            if (c != '0')
                fb[(uint32_t)(oy + y) * FRONTBUFFER_ROW_STRIDE_BYTES + ox + x] =
                    (uint8_t)(c <= '9' ? c - '0' : c - 'A' + 10);
        }
}

/* A flag, its RGB put in the nearest colours of the palette the screen is drawn in. */
static void draw_flag(int16_t flag, int32_t ox, int32_t oy)
{
    static uint8_t pal[256][3];
    uint8_t *fb = lobby_fb();
    const int32_t fx = (flag % kFlagCols) * kFlagW, fy = (flag / kFlagCols) * kFlagH;
    int32_t i, x, y;
    for (i = 0; i < 256; i++)
        Platform_PaletteRgb(i, &pal[i][0], &pal[i][1], &pal[i][2]);
    for (y = 0; y < kFlagH; y++)
        for (x = 0; x < kFlagW; x++) {
            const uint8_t *c = &k_flagAtlasRgb[((fy + y) * kFlagAtlasW + fx + x) * 3];
            int32_t best = 0, bestD = 0x7fffffff;
            for (i = 0; i < 256; i++) {
                const int32_t dr = c[0] - pal[i][0], dg = c[1] - pal[i][1], db = c[2] - pal[i][2];
                const int32_t d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
                if (d < bestD) {
                    bestD = d;
                    best = i;
                }
            }
            fb[(uint32_t)(oy + y) * FRONTBUFFER_ROW_STRIDE_BYTES + ox + x] = (uint8_t)best;
        }
}

static int32_t lobby_row_of(MenuEntry *e, int32_t ordinal01)
{
    return e == entry_at(ordinal01) ? 0 : e == entry_at(ordinal01 + 1) ? 1 : -1;
}

/* SWOS 2020's drawUniformSpritesInLobby / drawUniformSprite: the kit a team plays in (its first,
 * which the UNIFORM screen's choice becomes online) as a little player running on the spot, from
 * the front and from the back, under the table -- the top row's on the left (under LOCATIONS),
 * the other's on the right (under GO). Its frames 0 1 2 1, ten 50ths of a second each. */
static void draw_uniform(const KitLook *k, int32_t type, int32_t frame, int32_t ox, int32_t oy)
{
    extern uint16_t _kTeamColorsTable[];
    static const int8_t shift[2][3] = { { -1, 0, -2 }, { 0, -1, 1 } };     /* (SWOS 2020's offset[][]) */
    static const struct { char c; uint8_t colour; } kFixed[8] = {
        { 'a', 5 }, { 'b', 4 }, { 'c', 12 }, { 'd', 6 }, { 'e', 5 }, { 'f', 2 }, { 'g', 1 }, { 'h', 3 },
    };
    uint8_t *fb = lobby_fb();
    int32_t i, j, n;
    for (j = 0; j < 14; j++)
        for (i = 0; i < 16; i++) {
            const int32_t col = i + 48 * type + 16 * frame + shift[type][frame];
            const char c = col >= 0 && col < 96 ? k_uniformSprite[k->type & 3][j][col] : '.';
            int32_t colour = -1;
            if (c == '0')      colour = _kTeamColorsTable[k->stripes % KIT_COLOURS];
            else if (c == '1') colour = _kTeamColorsTable[k->basic % KIT_COLOURS];
            else if (c == '2') colour = _kTeamColorsTable[k->shorts % KIT_COLOURS];
            else if (c == '3') colour = _kTeamColorsTable[k->socks % KIT_COLOURS];
            else
                for (n = 0; n < 8; n++)
                    if (kFixed[n].c == c)
                        colour = kFixed[n].colour;
            if (colour >= 0)
                fb[(uint32_t)(oy + j) * FRONTBUFFER_ROW_STRIDE_BYTES + ox + i] = (uint8_t)colour;
        }
}

static void lobby_kits(int32_t row, int32_t x, int32_t y)
{
    static const int8_t cycle[4] = { 0, 1, 2, 1 };
    const LobbyPlayer *p = NetplayLobby_Player(row + 1);
    const int32_t frame = cycle[(SDL_GetTicks() / 200) % 4];
    KitLook look;
    if (!p->known || !p->hasTeam || NetplayLobby_State() == LOBBY_FAILED)
        return;
    KitArt_FromTeam(&p->team.team, 0, &look);
    draw_uniform(&look, 1, frame, x, y);                /* the front */
    draw_uniform(&look, 0, frame, x + 15, y);           /* the back */
}

static void location_AfterDraw(MenuEntry *e)
{
    const int32_t r = lobby_row_of(e, MpGameSetupMenu_location_01);
    if (r >= 0 && s_locFlag[r] >= 0)
        draw_flag(s_locFlag[r], e->x + 3, e->y + (e->dwHeight - kFlagH) / 2);
    if (r == 1)
        lobby_kits(0, e->x + 2, e->y + e->dwHeight + 2);
}

void OnlineMenus_DrawStars(int32_t srn, int32_t x, int32_t y)
{
    put_map(&k_starsMap[star_level(srn) * 10], 10, 0, 36, x, y);
}

static void player_AfterDraw(MenuEntry *e)
{
    const int32_t r = lobby_row_of(e, MpGameSetupMenu_cPlayers_01);
    if (r >= 0 && s_stars[r] >= 0)
        put_map(&k_starsMap[s_stars[r] * 10], 10, 0, 36, e->x + e->dwWidth - 36 - 2,
                e->y + e->dwHeight / 2 - 4);            /* (SWOS 2020's drawGoButtonAndStarsGraphics) */
}

static void go_AfterDraw(MenuEntry *e)
{
    const int32_t r = lobby_row_of(e, MpGameSetupMenu_go_01);
    if (r >= 0 && s_goState[r] >= 0)
        put_map(k_checkSymbols, 13, 12 * (s_goState[r] ? 2 : 3), 12, e->x + (e->dwWidth - 7) / 2,
                e->y + e->dwHeight / 2 - 4);            /* (as the stars; the tick and cross are 7 wide) */
    if (r == 1)
        lobby_kits(1, e->x + e->dwWidth - 15 - 16 - 1, e->y + e->dwHeight + 2);
}

static void chat_arrow_AfterDraw(MenuEntry *e)
{
    extern void United_DrawPageArrow(int32_t down, int32_t x, int32_t y);
    United_DrawPageArrow(e == entry_at(MpGameSetupMenu_scrollDown), e->x + 1, e->y + 1);
}

static void text_flags(int32_t ordinal, uint16_t color, uint16_t align)
{
    MenuEntry *e = entry_at(ordinal);
    e->stringFlags = (uint16_t)((e->stringFlags & ~(STR_COLOR_MASK | (1u << 15) | (1u << 14))) | color | align);
}

/* Every draw, after lobby_layout (which hides what OpenSWOS does not have). */
static void lobby2020_layout(void)
{
    const char *tag = World_Tag();
    int32_t i;
    for (i = 0; i < 3; i++) {
        set_visible(MpGameSetupMenu_location_00 + i, 1);
        entry_at(MpGameSetupMenu_location_00 + i)->isDisabled = 1;
    }
    set_entry(MpGameSetupMenu_location_00, "LOC.", 0, kGray);       /* (SWOS 2020 since its flags) */
    /* LOC. 4 wider (room between the flag and the country), GO 4 narrower; the columns between
     * them move along. From LOC.'s own x, so every draw puts them in the same place. */
    for (i = 0; i < 3; i++) {
        const int32_t L = entry_at(MpGameSetupMenu_location_00 + i)->x;
        entry_at(MpGameSetupMenu_location_00 + i)->dwWidth = 30 + 4;
        entry_at(MpGameSetupMenu_cPlayers_00 + i)->x = (uint16_t)(L + 32 + 4);
        entry_at(MpGameSetupMenu_teams_00 + i)->x    = (uint16_t)(L + 140 + 4);
        entry_at(MpGameSetupMenu_tactics_00 + i)->x  = (uint16_t)(L + 238 + 4);
        entry_at(MpGameSetupMenu_go_00 + i)->x       = (uint16_t)(L + 284 + 4);
        entry_at(MpGameSetupMenu_go_00 + i)->dwWidth = 36 - 4;
    }
    snprintf(s_teamDb1, sizeof s_teamDb1, "%.2s/%.2s", tag, strlen(tag) >= 4 ? tag + 2 : "");
    snprintf(s_teamDb, sizeof s_teamDb, "TEAM DB: %s", s_teamDb1);
    set_visible(MpGameSetupMenu_teamDB, 1);
    set_text(MpGameSetupMenu_teamDB, s_teamDb);
    set_visible(MpGameSetupMenu_teamDB1Label, 1);
    set_visible(MpGameSetupMenu_teamDB1, 1);
    set_entry(MpGameSetupMenu_teamDB1, s_teamDb1, 0, kGray);      /* (the world each game plays: not the lobby's) */
    /* LATENCY: right-aligned under CONNECTED, as SWOS 2020 places it (its prediction figure,
     * latencyPercent, is not drawn here) */
    {
        const MenuEntry *cs = entry_at(MpGameSetupMenu_connectionStatus);
        MenuEntry *ping = entry_at(MpGameSetupMenu_latencyPing), *label = entry_at(MpGameSetupMenu_latencyLabel);
        ping->x = (uint16_t)(cs->x + cs->dwWidth - ping->dwWidth);
        label->x = (uint16_t)(ping->x - label->dwWidth - 2);
        text_flags(MpGameSetupMenu_latencyPing, kWhiteText2, 1u << 14);
        text_flags(MpGameSetupMenu_latencyLabel, kWhiteText2, 1u << 14);
    }
    /* the chat's arrows: SWOS 2020's red ones, nothing behind them */
    entry_at(MpGameSetupMenu_scrollUp)->background = entry_at(MpGameSetupMenu_scrollDown)->background = 0;
    set_text(MpGameSetupMenu_scrollUp, " ");
    set_text(MpGameSetupMenu_scrollDown, " ");
    entry_at(MpGameSetupMenu_scrollUp)->afterDrawCb = entry_at(MpGameSetupMenu_scrollDown)->afterDrawCb =
        chat_arrow_AfterDraw;
    for (i = 0; i < 2; i++) {
        entry_at(MpGameSetupMenu_location_01 + i)->afterDrawCb = location_AfterDraw;
        entry_at(MpGameSetupMenu_cPlayers_01 + i)->afterDrawCb = player_AfterDraw;
        entry_at(MpGameSetupMenu_go_01 + i)->afterDrawCb = go_AfterDraw;
    }
}

/* One player's row, SWOS 2020's way (after OpenSWOS's has set what each may do). */
static char s_rowName2[2][LOBBY_NAME_MAX + 4], s_rowTeam2[2][24];
/* FIRE on a name: their MATCH INFO. While this player is in it the match cannot start (as in the
 * team list), so their GO goes down for that time and comes back if it stood. */
static void lobby_player_onSelect(MenuEntry *unused)
{
    extern uint8_t _currentMenu[];
    const MenuEntry *e = ((Menu *)_currentMenu)->selectedEntry;
    const int32_t me = NetplayLobby_LocalPlayer();
    const LobbyPlayer *p = NetplayLobby_Player(e == entry_at(MpGameSetupMenu_cPlayers_01) ? 1 : 2);
    const LobbyPlayer *mine = NetplayLobby_Player(me);
    const int32_t wasReady = mine && mine->ready;
    (void)unused;
    if (!p || !p->name[0])
        return;
    if (wasReady) {
        NetplayLobby_SetReady(0);
        NetplayLobby_Poll();
    }
    Account_ShowPlayer(p->name);
    if (wasReady && NetplayLobby_State() == LOBBY_OPEN)
        NetplayLobby_SetReady(1);
}

static void lobby2020_row(int32_t row, const LobbyPlayer *p, int32_t canEdit)
{
    extern uint8_t _smallCharsTable[];
    char country[32];
    int32_t srn = 0, i, w = 0;
    /* the other player gone (the link down): both rows "-", as SWOS 2020 empties its table when
     * the connection is lost -- no names, flags, stars, kits or GO left behind */
    const int32_t gone = NetplayLobby_State() == LOBBY_FAILED;
    const int32_t known = !gone && p->known && p->name[0];
    const int32_t info = known && Account_PlayerInfo(p->name, country, sizeof country, &srn);
    const char *iso = info && country[0] ? country_iso2(country) : NULL;
    MenuEntry *pe = entry_at(MpGameSetupMenu_cPlayers_01 + row);

    s_locFlag[row] = known ? (iso ? flag_of(iso) : 0) : -1;
    snprintf(s_locText[row], sizeof s_locText[row], known ? "     %.2s" : "-", iso ? iso : "XX");
    set_entry(MpGameSetupMenu_location_01 + row, s_locText[row], 0, kPurple);
    s_stars[row] = info ? star_level(srn) : -1;

    /* the name from the left, shortened with a dot clear of the stars (SWOS 2020's shortenName) */
    snprintf(s_rowName2[row], sizeof s_rowName2[row], gone ? "-" : " %s", known ? p->name : "WAITING...");
    for (i = 0; s_rowName2[row][i]; i++) {
        uint8_t one[2] = { (uint8_t)s_rowName2[row][i], 0 };
        w += GetTextWidth(one, _smallCharsTable) - ((CharsTableHeader *)_smallCharsTable)->charSpacing;
        if (s_stars[row] >= 0 && w > (int32_t)pe->dwWidth - 36 - 8) {
            s_rowName2[row][i] = '.';
            s_rowName2[row][i + 1] = 0;
            break;
        }
    }
    set_entry(MpGameSetupMenu_cPlayers_01 + row, s_rowName2[row], known, kPurple);
    pe->onSelect = known ? lobby_player_onSelect : NULL;
    text_flags(MpGameSetupMenu_cPlayers_01 + row, kWhiteText2, gone ? 0 : kLeftAligned);

    /* TEAMS (from the left) and TACTICS light brown, GO red with its symbol */
    if (gone) {
        set_text(MpGameSetupMenu_teams_01 + row, "-");
        set_text(MpGameSetupMenu_tactics_01 + row, "-");
    } else if (p->hasTeam) {
        snprintf(s_rowTeam2[row], sizeof s_rowTeam2[row], " %.17s", (char *)p->team.team.teamName);
        set_text(MpGameSetupMenu_teams_01 + row, s_rowTeam2[row]);
    }
    entry_at(MpGameSetupMenu_teams_01 + row)->bgColor = known ? kLightBrownWithYellowFrame : kGray;
    text_flags(MpGameSetupMenu_teams_01 + row, kWhiteText2, gone ? 0 : kLeftAligned);
    entry_at(MpGameSetupMenu_tactics_01 + row)->bgColor = known ? kLightBrownWithYellowFrame : kGray;
    if (!gone && p->hasTeam && p->tactic >= TACTIC_USER_A)
        set_text(MpGameSetupMenu_tactics_01 + row, "CUSTOM");     /* (SWOS 2020's get_tactics_name) */
    s_goState[row] = known ? (int8_t)(p->ready ? 1 : 0) : -1;
    set_text(MpGameSetupMenu_go_01 + row, known ? " " : "-");
    entry_at(MpGameSetupMenu_go_01 + row)->bgColor = !known ? kGray : p->ready ? kGreen : kRed;   /* (SWOS 2020's) */
    (void)canEdit;
}
#endif /* PROJECT_SWOS2020_MATCHMAKING */

/* Who plays whom, for Discord (include/live_ticker.h): at kick-off, the match's rules in force. */
static void ticker_begin(void)
{
    const LobbyRules *rules = NetplayLobby_Rules();
    const char *tag = World_Tag();
    const char *tour = Account_MatchTournament();
    LiveTickerMatch m;
    int32_t p;

    memset(&m, 0, sizeof m);
    for (p = 0; p < 2; p++) {
        const LobbyPlayer *lp = NetplayLobby_Player(p + 1);
        char country[40] = "", tactic[24];
        int32_t srn, i, o = 0;
        if (!lp)
            continue;
        snprintf(m.name[p], sizeof m.name[p], "%s", lp->name);
        for (i = 0; i < 17 && lp->team.team.teamName[i] && o + 1 < (int32_t)sizeof m.team[p]; i++)
            if (o || lp->team.team.teamName[i] != ' ')
                m.team[p][o++] = (char)lp->team.team.teamName[i];
        m.team[p][o] = 0;
        snprintf(m.tactic[p], sizeof m.tactic[p], "%s", tactic_name(lp, tactic, sizeof tactic));
        if (Account_PlayerInfo(lp->name, country, sizeof country, &srn)) {
            const char *iso2 = country_iso2(country);
            if (iso2) {
                snprintf(m.country[p], sizeof m.country[p], "%s", iso2);
                for (i = 0; i < (int32_t)(sizeof k_countryNames / sizeof k_countryNames[0]); i++)
                    if (strcmp(k_countryNames[i][1], iso2) == 0) {
                        snprintf(m.location[p], sizeof m.location[p], "%s", k_countryNames[i][0]);
                        break;
                    }
                if (!m.location[p][0] && strncmp(iso2, "GB-", 3) == 0)
                    snprintf(m.location[p], sizeof m.location[p], "%s",
                             iso2[3] == 'E' ? "ENGLAND" : iso2[3] == 'S' ? "SCOTLAND" : iso2[3] == 'W' ? "WALES"
                                                                                         : "NORTHERN IRELAND");
            }
        }
    }
    m.mode = rules->mode;
    snprintf(m.tournament, sizeof m.tournament, "%s", tour ? tour : "");
    m.style = _gGameplayStyle;
    snprintf(m.teamDb, sizeof m.teamDb, "%.2s/%.2s", tag, strlen(tag) >= 4 ? tag + 2 : "");
    m.legs = rules->legs;
    m.leg = rules->leg;
    m.firstLeg1 = rules->firstLeg1;
    m.firstLeg2 = rules->firstLeg2;
    m.host = NetplayLobby_IsHost();
    m.localPlayer = NetplayLobby_LocalPlayer();
    LiveTicker_Begin(&m);
}

#if PROJECT_SWOS2020_MATCHMAKING
static void style_button_AfterDraw(MenuEntry *e)
{
    United_DrawStyleIcon(e == entry_at(HostGameMessageMenu_button1) ? 0
                         : e == entry_at(HostGameMessageMenu_button2) ? 1 : 2, e->x + 32, e->y + 2);
}

/* CHOOSE GAMEPLAY, SWOS United's: HOST MATCH, the three styles as ONLINE RANKINGS' buttons. */
static int32_t fork_host_style_layout(void)
{
    set_text(HostGameMessageMenu_header,   "HOST MATCH");    /* (MATCH MAKING's button) */
    set_text(HostGameMessageMenu_message1, "CHOOSE GAMEPLAY");   /* (SWOS United's words) */
    set_text(HostGameMessageMenu_message2, "");
    set_text(HostGameMessageMenu_message3, "");
    /* the three buttons as ONLINE RANKINGS' (rankingsMenu: 125 x 20, green, 36 apart, the
     * style's icon 32 in -- united_api.h), centred on the menu as those are */
    {
        MenuEntry *b1 = entry_at(HostGameMessageMenu_button1), *b2 = entry_at(HostGameMessageMenu_button2);
        MenuEntry *b3 = entry_at(HostGameMessageMenu_button3);
        const int32_t centre = b1->x + b1->dwWidth / 2;
        /* the panel half as tall (one line, CHOOSE GAMEPLAY, centred in it), and the three
         * buttons centred between it and EXIT -- from the loaded menu's y, never absolute */
        MenuEntry *panel = entry_at(HostGameMessageMenu_panel), *line = entry_at(HostGameMessageMenu_message1);
        const int32_t exitTop = entry_at(HostGameMessageMenu_exit)->y;
        int32_t below, top;
        panel->dwHeight = (uint16_t)(panel->dwHeight / 2);
        line->y = (uint16_t)(panel->y + ((int32_t)panel->dwHeight - (int32_t)line->dwHeight) / 2);
        below = panel->y + panel->dwHeight;
        top = below + (exitTop - below - (20 + 16 + 20 + 16 + 20)) / 2;
        set_entry(HostGameMessageMenu_button1, "     AMIGA", 1, kGreen);     /* (rankingsMenu's, */
        set_entry(HostGameMessageMenu_button2, "     PC DOS", 1, kGreen);    /*  clear of the icon) */
        set_entry(HostGameMessageMenu_button3, "     XBOX", 1, kGreen);      /* (SWOS 2020's third style) */
        set_visible(HostGameMessageMenu_button3, 1);
        b1->dwWidth = b2->dwWidth = b3->dwWidth = 125;
        b1->dwHeight = b2->dwHeight = b3->dwHeight = 20;
        b1->x = b2->x = b3->x = (uint16_t)(centre - 125 / 2);
        b1->y = (uint16_t)top;
        b2->y = (uint16_t)(top + 36);
        b3->y = (uint16_t)(top + 72);
        b1->afterDrawCb = b2->afterDrawCb = b3->afterDrawCb = style_button_AfterDraw;
        lobby_nav(HostGameMessageMenu_button2, -1, -1, HostGameMessageMenu_button1, HostGameMessageMenu_button3);
        lobby_nav(HostGameMessageMenu_button3, -1, -1, HostGameMessageMenu_button2, HostGameMessageMenu_exit);
        lobby_nav(HostGameMessageMenu_exit, -1, -1, HostGameMessageMenu_button3, -1);
    }
    return 1;
}

static void fork_join_requested(const char *who)
{
    (void)who;
    online_sound("sound_dos/join.wav");         /* (SWOS 2020's playJoinSound) */
}

static void fork_auto_joining(const char *owner)
{
    snprintf(s_mm2AskedOwner, sizeof s_mm2AskedOwner, "%s", owner);
}

/* MATCH MAKING, SWOS 2020's table: the request a host turned down, or a host gone, then the table. */
static void fork_matchmaking_draw(void)
{
    static uint32_t removedUntil;
    static int32_t refused;
    if (MmClient_TakeRemoved()) {
        removedUntil = SDL_GetTicks() + 4000;
        refused = mm2_still_open(s_mm2AskedOwner);
        s_mm2AskedOwner[0] = '\0';
    }
    mm2_draw();
    if (removedUntil && SDL_GetTicks() < removedUntil)
        set_entry(MatchMakingMenu_desc, refused ? "YOU ARE NOT ALLOWED TO JOIN" : "THE HOST LEFT THE GAME", 0, 0);
}

static void fork_lobby_layout(void)
{
    const int32_t row = NetplayLobby_LocalPlayer();
    const int32_t teams = MpGameSetupMenu_teams_00 + row;
    /* LEFT from TEAMS goes to LOCATIONS (SWOS 2020's flags) */
    lobby_nav(teams, MpGameSetupMenu_cPlayers_00 + row, MpGameSetupMenu_tactics_00 + row, -1, MpGameSetupMenu_legOpt1);
    lobby_nav(MpGameSetupMenu_cPlayers_01, -1, row == 1 ? teams : -1, -1, MpGameSetupMenu_cPlayers_02);
    lobby_nav(MpGameSetupMenu_cPlayers_02, -1, row == 2 ? teams : -1, MpGameSetupMenu_cPlayers_01, -1);
    lobby2020_layout();
}

static const char *fork_style_name(int32_t style)
{
    return style == GAMEPLAY_DOS ? "PC DOS" : style == GAMEPLAY_XBOX ? "XBOX" : "AMIGA";   /* (SWOS 2020's) */
}

static void fork_lobby_status(int32_t st)
{
    text_flags(MpGameSetupMenu_connectionStatus, st == LOBBY_OPEN || st == LOBBY_KICKOFF ? kYellowText : kWhiteText2,
               (uint16_t)(entry_at(MpGameSetupMenu_connectionStatus)->stringFlags & ((1u << 15) | (1u << 14))));
}

static int32_t fork_chat_color(const LobbyChatLine *l)
{
    if (l->who != LOBBY_CHAT_BOT)
        return strncmp(l->text, "USING ", 6) == 0 ? kGreenText : kWhiteText2;
    return strncmp(l->text, "SRN ", 4) == 0 ? kYellowText : kGreenText;   /* (SWOS 2020's colours) */
}

/* GO: getting ready says what this player plays with, in the chat, as SWOS 2020's
 * selectStatus1/2 do ("USING <controller>", the pad's name or KEYBOARD). */
static void fork_readying(void)
{
    extern const char *InputSDL_Player1DeviceName(void);
    const char *pad = InputSDL_Player1DeviceName();
    char line[40];
    uint32_t i;
    snprintf(line, sizeof line, "USING %s", pad ? pad : "KEYBOARD");
    for (i = 0; line[i]; i++)
        if (line[i] >= 'a' && line[i] <= 'z')
            line[i] = (char)(line[i] - 'a' + 'A');
    NetplayLobby_Say(line);             /* (cut to a chat line's width) */
}
#endif /* PROJECT_SWOS2020_MATCHMAKING */

/* The joiner, accepted (SWOS 2020's playAcceptSound): once, not on each return from a match. */
static void fork_lobby_entered(int32_t player)
{
    if (player == 2 && accept_sound_on())
        online_sound("sound_dos/accept_1.wav");
}

static void fork_lobby_left(void)
{
#if PROJECT_SWOS2020_MATCHMAKING
    KitArt_Return();            /* (the kits' palette entries, if the Jaguar's art lent them) */
#endif
}

static void fork_live_start(void) { ticker_begin(); }
static void fork_live_end(int32_t finished, const char *why) { LiveTicker_End(finished, why); }

static void fork_settings_draw(void)
{
    set_text(MmSettingsMenu_acceptSound, accept_sound_on() ? "SOUND 1" : "OFF");   /* (SWOS 2020's words) */
}

static void fork_init(void)
{
    static const MenuSymbol handlers[] = {
        { "mmsAcceptSound", (void *)mmsAcceptSound, SYM_FUNC },
#if PROJECT_SWOS2020_MATCHMAKING
        { "matchMaking_action_onSelect", (void *)matchMaking_action_onSelect, SYM_FUNC },
        { "matchMaking_scrollUp_onSelect", (void *)matchMaking_scrollUp_onSelect, SYM_FUNC },
        { "matchMaking_scrollDown_onSelect", (void *)matchMaking_scrollDown_onSelect, SYM_FUNC },
        { "matchMaking_scrollUp2_onSelect", (void *)matchMaking_scrollUp2_onSelect, SYM_FUNC },
        { "matchMaking_scrollDown2_onSelect", (void *)matchMaking_scrollDown2_onSelect, SYM_FUNC },
#endif
    };
    MenuJson_RegisterSymbols(handlers, (int)(sizeof handlers / sizeof handlers[0]));
}

static const OnlineMenusFork *online_menus_fork(void)
{
    static const OnlineMenusFork fork = {
        .init            = fork_init,
#if PROJECT_SWOS2020_MATCHMAKING
        .ownsMatchMaking = 1,
        .matchMakingDraw = fork_matchmaking_draw,
        .joinRequested   = fork_join_requested,
        .autoJoining     = fork_auto_joining,
        .hostStyleLayout = fork_host_style_layout,
        .lobbyLayout     = fork_lobby_layout,
        .styleName       = fork_style_name,
        .lobbyStatus     = fork_lobby_status,
        .lobbyRow        = lobby2020_row,
        .chatColor       = fork_chat_color,
        .readying        = fork_readying,
#endif
        .lobbyEntered    = fork_lobby_entered,
        .lobbyLeft       = fork_lobby_left,
        .liveMatchStart  = fork_live_start,
        .liveMatchEnd    = fork_live_end,
        .settingsDraw    = fork_settings_draw,
    };
    return &fork;
}
