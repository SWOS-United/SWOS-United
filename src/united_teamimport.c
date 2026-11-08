/*
 * united_teamimport.c -- MY CUSTOM TEAMS' IMPORT FROM SWOS 2020 (the screen: src/custom_team.c).
 *
 * SWOS 2020 uploads a player's custom teams to sensiblesoccer.de as <USER>.TEAM, <USER>_2.TEAM and
 * <USER>_3.TEAM (its uploadCustomTeamToServer), in capitals: PROJECT_SWOS2020_TEAMS. Each is the raw
 * 684-byte record, so a team here comes from it the way a .TEAM put in a slot does: CustomTeam_FromTeam
 * finds the database team by its PLAYERS, never by the number in the file (SWOS 2020 renumbers its
 * custom teams): all sixteen, by position, skills and price, against the 96/97 data. A team that
 * cannot be verified -- one player that does not match, one skill altered -- is refused, by name.
 *
 * The files are asked for by name, on a thread of their own; what they mean for the slots is decided
 * back on the menu's thread. Teams go to the empty slots. A team already in a slot is left there, so
 * pressing the button again adds nothing. With fewer empty slots than teams the player is asked
 * whether to overwrite: YES puts the rest over the slots that were in use, in order; NO fills the
 * empty slots and leaves the rest out. A slot holding one of the imported teams is never overwritten.
 */
#include "online_account.h"
#include "project_identity.h"

#if PROJECT_ACCOUNTS && defined(SWOS_SDL_BUILD) && defined(SWOS_HTTPS)
#include "united_api.h"
#include "united_ui.h"            /* OpenLeg, the screens */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "custom_team.h"
#include "https_client.h"
#include "game_types.h"

enum { RECORD = 684, FILES = 3 };

static struct {
    SDL_atomic_t done;
    int32_t      reached;               /* the site answered at all */
    int32_t      have[FILES];
    uint8_t      bytes[FILES][RECORD];
    char         url[FILES][256];
} *s_job;

static int32_t    s_state;              /* TEAM_IMPORT_* */
static int32_t    s_changed;            /* the slots changed: told once */
static TeamFileEx s_team[TEAM_IMPORT_MAX];
static int32_t    s_teamCount;
/* What there is to say: one pair of lines, or several in turn (a refused team, each by name). */
enum { MESSAGES = FILES + 1, MESSAGE_MS = 3000 };
static char       s_msg[MESSAGES][2][64];
static int32_t    s_msgCount;
static char       s_refused[FILES][2][64];
static int32_t    s_refusedCount;
static char       s_line1[64], s_line2[64];

static void say(const char *line1, const char *line2)
{
    snprintf(s_msg[0][0], sizeof s_msg[0][0], "%s", line1);
    snprintf(s_msg[0][1], sizeof s_msg[0][1], "%s", line2);
    s_msgCount = 1;
}

/* The refused teams' warnings after whatever has been said. */
static void say_refused(void)
{
    int32_t i;
    for (i = 0; i < s_refusedCount && s_msgCount < MESSAGES; i++) {
        memcpy(s_msg[s_msgCount], s_refused[i], sizeof s_msg[0]);
        s_msgCount++;
    }
}

static void refuse(const TeamFileEx *record)
{
    char name[20];
    int32_t i = 0, o = 0;
    while (i < 17 && record->team.teamName[i] == ' ')   /* (SWOS 2020's names start with a space) */
        i++;
    for (; i < 17 && record->team.teamName[i]; i++) {
        const char c = (char)record->team.teamName[i];
        if (c >= 0x20)
            name[o++] = (char)(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    }
    while (o > 0 && name[o - 1] == ' ')
        o--;
    name[o] = 0;
    snprintf(s_refused[s_refusedCount][0], sizeof s_refused[0][0], "'%s' CANNOT BE IMPORTED.", name);
    snprintf(s_refused[s_refusedCount][1], sizeof s_refused[0][1], "PLEASE CREATE IT MANUALLY.");
    s_refusedCount++;
    fprintf(stderr, "[united] %s.TEAM: its players are not a 96/97 team's: refused\n", name);
}

static int worker(void *arg)
{
    (void)arg;
    for (int32_t i = 0; i < FILES; i++) {
        HttpsReply r;
        memset(&r, 0, sizeof r);
        Https_Request(s_job->url[i], NULL, &r);
        if (r.status > 0)
            s_job->reached = 1;
        /* (a file that is not there is a redirect to a page: only a whole record is a team) */
        if (r.status == 200 && r.body && r.length == RECORD && r.body[0] != '{') {
            memcpy(s_job->bytes[i], r.body, RECORD);
            s_job->have[i] = 1;
        }
        Https_Free(&r);
    }
    SDL_AtomicSet(&s_job->done, 1);
    return 0;
}

void Account_TeamImportStart(void)
{
    const char *test = getenv("SWOS_UNITED_TEST_IMPORT");       /* <name>[:y|n]: another player's */
    char user[40], up[40], esc[120];
    uint32_t i;
    SDL_Thread *t;

    if (s_state == TEAM_IMPORT_BUSY)
        return;
    snprintf(user, sizeof user, "%s", test ? test : United_UserName());
    if (strchr(user, ':'))
        *strchr(user, ':') = 0;
    say("", "");
    if (!user[0]) {
        say("PLEASE LOG IN FIRST.", "");
        return;
    }
    s_job = calloc(1, sizeof *s_job);
    if (!s_job)
        return;
    for (i = 0; user[i] && i < sizeof up - 1; i++)
        up[i] = (char)(user[i] >= 'a' && user[i] <= 'z' ? user[i] - 'a' + 'A' : user[i]);
    up[i] = 0;
    Https_Escape(up, esc, sizeof esc);
    snprintf(s_job->url[0], sizeof s_job->url[0], PROJECT_SWOS2020_TEAMS "%s.TEAM", esc);
    snprintf(s_job->url[1], sizeof s_job->url[1], PROJECT_SWOS2020_TEAMS "%s_2.TEAM", esc);
    snprintf(s_job->url[2], sizeof s_job->url[2], PROJECT_SWOS2020_TEAMS "%s_3.TEAM", esc);
    t = SDL_CreateThread(worker, "teamimport", NULL);
    if (!t) {
        free(s_job);
        s_job = NULL;
        return;
    }
    SDL_DetachThread(t);
    s_state = TEAM_IMPORT_BUSY;
}

/* The slots this import may not touch: already holding one of the teams. */
static int32_t in_slot(const TeamFileEx *team, int32_t *kept)
{
    int32_t slot, found = 0;
    for (slot = 0; slot < CUSTOM_TEAM_SLOTS; slot++) {
        TeamFileEx have;
        if (CustomTeam_Load(slot, &have) && memcmp(&have, team, sizeof have) == 0) {
            kept[slot] = 1;
            found = 1;
        }
    }
    return found;
}

static void finish(int32_t overwrite)
{
    int32_t kept[CUSTOM_TEAM_SLOTS] = { 0 }, used[CUSTOM_TEAM_SLOTS], done = 0, left = 0, t, slot;
    char line[64];

    for (t = 0; t < s_teamCount; t++)
        in_slot(&s_team[t], kept);
    for (slot = 0; slot < CUSTOM_TEAM_SLOTS; slot++) {
        TeamFileEx have;
        used[slot] = CustomTeam_Load(slot, &have);
    }
    for (t = 0; t < s_teamCount; t++) {
        int32_t to = -1;
        for (slot = 0; slot < CUSTOM_TEAM_SLOTS && to < 0; slot++)
            if (!used[slot])
                to = slot;
        for (slot = 0; overwrite && slot < CUSTOM_TEAM_SLOTS && to < 0; slot++)
            if (used[slot] == 1 && !kept[slot])
                to = slot;
        if (to < 0) {
            left++;
            continue;
        }
        if (CustomTeam_Save(to, &s_team[t])) {
            used[to] = 2;               /* (this import's: not overwritten by the next team) */
            done++;
        }
    }
    s_changed = done > 0;
    snprintf(line, sizeof line, done == 1 ? "1 TEAM IMPORTED." : "%d TEAMS IMPORTED.", (int)done);
    say(line, left ? "THE OTHERS WERE LEFT OUT." : "");
    say_refused();
    s_state = TEAM_IMPORT_IDLE;
}

static void decide(void)
{
    const int32_t reached = s_job->reached;
    int32_t kept[CUSTOM_TEAM_SLOTS] = { 0 }, files = 0, i, empty = 0, slot;

    s_teamCount = s_refusedCount = 0;
    for (i = 0; i < FILES; i++) {
        TeamFileEx record;
        CustomTeam custom;
        if (!s_job->have[i])
            continue;
        files++;
        memcpy(&record, s_job->bytes[i], RECORD);
        if (!CustomTeam_FromTeam(&record, &custom) || !CustomTeam_Resolve(&custom, &s_team[s_teamCount])) {
            refuse(&record);
            continue;
        }
        if (!in_slot(&s_team[s_teamCount], kept))
            s_teamCount++;
    }
    free(s_job);
    s_job = NULL;
    s_state = TEAM_IMPORT_IDLE;
    if (!files) {
        if (reached)
            say("NO CUSTOM TEAMS FOUND IN SWOS 2020.", "PLEASE CREATE A TEAM MANUALLY.");
        else
            say("COULD NOT REACH SENSIBLESOCCER.DE.", "PLEASE TRY AGAIN.");
        return;
    }
    if (!s_teamCount) {
        s_msgCount = 0;
        if (!s_refusedCount)
            say("YOUR SWOS 2020 TEAMS ARE ALREADY HERE.", "");
        say_refused();
        return;
    }
    for (slot = 0; slot < CUSTOM_TEAM_SLOTS; slot++) {
        TeamFileEx have;
        empty += !CustomTeam_Load(slot, &have);
    }
    if (s_teamCount <= empty) {
        finish(0);
        return;
    }
    say("DO YOU WANT TO OVERWRITE", "YOUR EXISTING CUSTOM TEAM(S)?");
    s_state = TEAM_IMPORT_ASKING;
}

void Account_TeamImportAnswer(int32_t yes)
{
    if (s_state == TEAM_IMPORT_ASKING)
        finish(yes);
}

void Account_TeamImportReset(void)
{
    if (s_state != TEAM_IMPORT_BUSY) {
        s_state = TEAM_IMPORT_IDLE;
        say("", "");
    }
}

int32_t Account_TeamImportPoll(const char **line1, const char **line2, int32_t *changed)
{
    const char *test = getenv("SWOS_UNITED_TEST_IMPORT");
    if (s_state == TEAM_IMPORT_BUSY && SDL_AtomicGet(&s_job->done))
        decide();
    if (test && s_state == TEAM_IMPORT_ASKING && strchr(test, ':'))
        Account_TeamImportAnswer(strchr(test, ':')[1] == 'y');
    if (s_state == TEAM_IMPORT_ASKING || s_msgCount <= 1) {
        memcpy(s_line1, s_msg[0][0], sizeof s_line1);
        memcpy(s_line2, s_msg[0][1], sizeof s_line2);
    } else {
        const int32_t i = (int32_t)(SDL_GetTicks() / MESSAGE_MS % (uint32_t)s_msgCount);
        memcpy(s_line1, s_msg[i][0], sizeof s_line1);
        memcpy(s_line2, s_msg[i][1], sizeof s_line2);
    }
    *line1 = s_line1;
    *line2 = s_line2;
    *changed = s_changed;
    s_changed = 0;
    return s_state;
}

#else

void    Account_TeamImportStart(void)        {}
void    Account_TeamImportAnswer(int32_t yes) { (void)yes; }
void    Account_TeamImportReset(void)        {}
int32_t Account_TeamImportPoll(const char **line1, const char **line2, int32_t *changed)
{
    *line1 = *line2 = "";
    *changed = 0;
    return TEAM_IMPORT_IDLE;
}

#endif
