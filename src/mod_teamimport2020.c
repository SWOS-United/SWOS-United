/*
 * mod_teamimport2020.c -- the teamimport2020 mod (bin/mods/teamimport2020; static in SWOS United):
 * MY CUSTOM TEAMS' IMPORT FROM SWOS 2020 (Playaveli, b2b2920e / f7c39d5c). The logged-in player's
 * custom teams on sensiblesoccer.de, converted by their players into the slots
 * (src/united_teamimport.c, behind the account interface: Account_TeamImport*).
 *
 * The mod's overlay of swos2020/selectMyCustomTeams (its data_json/menus) is the game's menu with
 * five entries more: the button over the three slots, YES and NO for overwriting, and two lines
 * of text. Its onDraw is the game's slots screen (CustomTeam_SlotsDraw) and then the import's;
 * the slots wait while it decides what goes in them.
 */
#ifdef SWOS_SDL_BUILD

#include <stdint.h>

#include "mod_api.h"
#include "custom_team.h"
#include "online_account.h"
#include "swos2020_menus.h"     /* SelectMyCustomTeams_team1 .. _exit */
#include "game_types.h"
#include "menu_system.h"      /* MenuEntry, Menu, CalcMenuEntryAddress */

#define SLUG "teamimport2020"

/* The overlay's five entries, after the game's five (data_json/menus/swos2020/selectMyCustomTeams). */
enum {
    kImportTeams = SelectMyCustomTeams_numEntries,
    kImportYes, kImportNo, kImportLine1, kImportLine2,
};


static void slots_link(int32_t ordinal, int32_t left, int32_t right, int32_t up, int32_t down)
{
    MenuEntry *e = CalcMenuEntryAddress((uint16_t)ordinal);
    e->leftEntry = (int8_t)left;
    e->rightEntry = (int8_t)right;
    e->upEntry = (int8_t)up;
    e->downEntry = (int8_t)down;
    e->directionLeft = e->directionRight = e->directionUp = e->directionDown = -1;
}

/* IMPORT FROM SWOS 2020: the button, or YES and NO in its place, and the two lines under it. A
 * service without it, and the list a team is picked from, have none of them. */
static void on_draw(void)
{
    extern uint8_t _currentMenu[];
    static char label[] = "IMPORT FROM SWOS 2020", busy[] = "CHECKING SWOS 2020...", yes[] = "YES", no[] = "NO";
    const int32_t on = !CustomTeam_SlotsPicking() && Account_Has(ACCOUNT_HAS_TEAM_IMPORT);
    Menu *menu = (Menu *)_currentMenu;
    const char *line1 = "", *line2 = "";
    int32_t state = TEAM_IMPORT_IDLE, changed = 0, i;

    if (on)
        state = Account_TeamImportPoll(&line1, &line2, &changed);
    if (changed)
        CustomTeam_SlotsReload();
    CustomTeam_SlotsDraw();                 /* the game's slots screen, then the import over it */

    for (i = kImportTeams; i <= kImportLine2; i++)
        CalcMenuEntryAddress((uint16_t)i)->isInvisible = !on;
    if (!on)
        return;
    {   /* the four at one pitch, the button first */
        const MenuEntry *top = CalcMenuEntryAddress(kImportTeams);
        for (i = 0; i < CUSTOM_TEAM_SLOTS; i++)
            CalcMenuEntryAddress((uint16_t)(SelectMyCustomTeams_team1 + i))->y = (uint16_t)(top->y + (i + 1) * 32);
    }
    for (i = 0; i < CUSTOM_TEAM_SLOTS; i++)         /* (the slots wait while it decides) */
        if (state != TEAM_IMPORT_IDLE)
            CalcMenuEntryAddress((uint16_t)(SelectMyCustomTeams_team1 + i))->isDisabled = 1;
    CalcMenuEntryAddress(kImportTeams)->isDisabled = state != TEAM_IMPORT_IDLE;
    CalcMenuEntryAddress(kImportYes)->isInvisible = state != TEAM_IMPORT_ASKING;
    CalcMenuEntryAddress(kImportNo)->isInvisible = state != TEAM_IMPORT_ASKING;
    CalcMenuEntryAddress(kImportTeams)->fg = (uintptr_t)(state == TEAM_IMPORT_BUSY ? busy : label);
    CalcMenuEntryAddress(kImportTeams)->bgColor = state == TEAM_IMPORT_IDLE ? 14 : 7;
    CalcMenuEntryAddress(kImportYes)->fg = (uintptr_t)yes;
    CalcMenuEntryAddress(kImportNo)->fg = (uintptr_t)no;
    CalcMenuEntryAddress(kImportLine1)->fg = (uintptr_t)line1;
    CalcMenuEntryAddress(kImportLine2)->fg = (uintptr_t)line2;
    CalcMenuEntryAddress(kImportLine1)->isDisabled = 1;
    CalcMenuEntryAddress(kImportLine2)->isDisabled = 1;

    if (state == TEAM_IMPORT_ASKING) {
        slots_link(kImportYes, -1, kImportNo, -1, SelectMyCustomTeams_exit);
        slots_link(kImportNo, kImportYes, -1, -1, SelectMyCustomTeams_exit);
        slots_link(SelectMyCustomTeams_exit, -1, -1, kImportNo, -1);
        if (menu->selectedEntry != CalcMenuEntryAddress(kImportYes) &&
            menu->selectedEntry != CalcMenuEntryAddress(SelectMyCustomTeams_exit))
            menu->selectedEntry = CalcMenuEntryAddress(kImportNo);     /* (nothing lost by default) */
    } else {
        slots_link(kImportTeams, -1, -1, -1, SelectMyCustomTeams_team1);
        slots_link(SelectMyCustomTeams_team1, -1, -1, kImportTeams, SelectMyCustomTeams_team2);
        slots_link(SelectMyCustomTeams_team2, -1, -1, SelectMyCustomTeams_team1, SelectMyCustomTeams_team3);
        slots_link(SelectMyCustomTeams_team3, -1, -1, SelectMyCustomTeams_team2, SelectMyCustomTeams_exit);
        slots_link(SelectMyCustomTeams_exit, -1, -1, SelectMyCustomTeams_team3, -1);
        if (menu->selectedEntry == CalcMenuEntryAddress(kImportYes) ||
            menu->selectedEntry == CalcMenuEntryAddress(kImportNo))
            menu->selectedEntry = CalcMenuEntryAddress(kImportTeams);
    }
}

static void on_init(void)    { Account_TeamImportReset(); }
static void import_start(void) { Account_TeamImportStart(); }
static void import_yes(void) { Account_TeamImportAnswer(1); }
static void import_no(void)  { Account_TeamImportAnswer(0); }

static void init(void)
{
    static const ModHandler handlers[] = {
        { "onInit", on_init }, { "onDraw", on_draw },
        { "import", import_start }, { "importYes", import_yes }, { "importNo", import_no },
    };
    ModApi_RegisterHandlers(SLUG, handlers, (int32_t)(sizeof handlers / sizeof handlers[0]));
}

const ModDef ModDef_teamimport2020 = {
    .slug = SLUG,
    .init = init,
};

#endif /* SWOS_SDL_BUILD */
