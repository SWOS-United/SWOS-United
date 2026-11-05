/* custom_team.c -- MY CUSTOM TEAM. SDL only. docs/CUSTOM_TEAMS.md, docs/TEAM_IDS.md.
 *
 * A custom team is a team of the database that its owner has renamed and redressed:
 * the team's name and coach; the players' names, faces, nationalities and shirt
 * numbers; the two kits; and the line-up and formation it starts in. It is NOT a team
 * with different players: every player's position, skills and value are the
 * database's. That rule is the whole design, and it is kept in three places:
 *
 *   THE FILE HAS NO PLAYER DATA. CUSTOMn.TEAM in the save folder is a CustomTeam
 *   (custom_team.h) as JSON: the ID of the database team it was made from, that team's
 *   checksum, and what the owner changed -- each player being "the database's player
 *   <id>" under another name. Loading it RESOLVES it: the team is looked up by its id
 *   (src/team_db.c), has to be the data the checksum says, and gives every position,
 *   skill and value. There is no number in the file to edit.
 *
 *   THE EDITOR is the game's own EDIT TEAM screen, which never could edit skills. Here
 *   its IMPORT takes a whole database team (identity included) and its UNDO button is
 *   LINEUP, the team screen a match shows, as SWOS 2020's custom team editor has it.
 *
 *   A TEAM FROM THE OTHER PLAYER is checked on arrival (CustomTeam_Allowed): it has to
 *   be a database team with only those things changed, or the lobby treats that player
 *   as having no team. That covers every team, custom or not.
 *
 * SWOS 2020, and the team editors, keep a team as the raw 684-byte team record (.TEAM).
 * A file like that put in the save folder under a slot's name is CONVERTED: the database
 * team it was made from is found by its players (not by the number in the record, which
 * SWOS 2020 sets to a custom team's own), the differences become a CustomTeam, and the
 * slot is rewritten as JSON, the original kept beside it as CUSTOMn.TEAM.684. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <strings.h>

#include "custom_team.h"
#include "project_identity.h"   /* PROJECT_TEAM_FORMAT */
#include "endian_helpers.h"
#include "kit_art.h"
#include "team_colors.h"
#include "menu_load_json.h"
#include "menu_symbols.h"
#include "menu_system.h"
#include "mjson.h"
#include "netplay_lobby.h"      /* LOBBY_TACTIC_MAX */
#include "online_account.h"     /* IMPORT FROM SWOS 2020, on a service that has it */
#include "swos2020_menus.h"
#include "swos_io.h"
#include "team_db.h"
#include "team_proto.h"
#include "world.h"
#include "teams.h"

extern TeamFileEx  _selectedTeams[SELECTED_TEAMS_MAX];
extern TeamFileEx *_selectedTeamPtr;
extern uint16_t    _selectedTeamCountryNumber;
extern uint16_t    _numSelectedTeams, _currentTeamNumber, _selTeamNumber;
extern uint16_t    _gameType, _isCareer, _trainingGame, _exitGameFlag;
extern uint8_t     _teamFileBuffer[];
extern const char *g_playMatchStartLabel;
extern int32_t     g_playMatchNoUniform;

extern int16_t WriteFile(uint8_t *filename, uint8_t *src, uint32_t size);
extern void    ChooseTeamsDialog(uint16_t showClub, uint16_t showNational, uint16_t showOrigCustom,
                                 uint8_t *title, void *onSelect);
extern void    EditTeamsCallback(TeamFileEx *team);
extern int16_t ShowPlayMatchMenu(int16_t controlFlags, int16_t maxSubstitutes, int16_t excludeFlags,
                                 int16_t isFirstPlayer, int16_t squadChanges,
                                 TeamFileEx *team1, TeamFileEx *team2);
extern void    SetExitMenuFlag(void);

enum {
    kPlayers = TEAM_PLAYERS,
    kTeamRecord = 684,
    kFaces = 4,                     /* EditTeamsChangePlayerFace: white, ginger, black, black with blond hair */
    kFormatVersion = 3,             /* 3: links to the database by id; no player data */
    kNationalities = 214,           /* the three-letter names a nationality indexes (_shortCountryNames) */
    kMaxShirtNumber = 99,
    kCountries = 256,
    kJsonMax = 8192,
    kPlayerCoach = 2,               /* TEAM_PLAYER_COACH: a side whose line-up can be arranged */
};

int32_t g_customTeamQuiet;          /* the self-test's and the converter's: they refuse on purpose */

/* ======================================================================
 * The rule: which database team a team is, player for player
 * ====================================================================== */

/* What makes a player the player he is: his position, his skills and his value --
 * everything a match plays him by. A custom team may change none of it. His name, face,
 * nationality and shirt number are the owner's: they are shown, not played (and the team
 * screen itself swaps two players' numbers when it swaps a starter and a substitute). */
static int32_t same_player(const PlayerFile *a, const PlayerFile *b)
{
    return (a->positionAndFace & PF_POSITION_MASK) == (b->positionAndFace & PF_POSITION_MASK) &&
           a->passing == b->passing && a->velocityHeading == b->velocityHeading &&
           a->tacklingControl == b->tacklingControl && a->speedFinishing == b->speedFinishing &&
           a->price == b->price;
}

/* Which of the database team's players each of `team`'s is: his own place first (the
 * usual case), else any that nobody has claimed. 0 if one of them is nobody's. */
static int32_t match_players(const TeamFileEx *team, const TeamFileEx *base, uint8_t is[kPlayers])
{
    uint8_t taken[kPlayers] = { 0 };
    int32_t i, j;

    for (i = 0; i < kPlayers; i++)
        is[i] = 0xFF;
    for (i = 0; i < kPlayers; i++)
        if (same_player(&team->players[i], &base->players[i])) {
            is[i] = (uint8_t)i;
            taken[i] = 1;
        }
    for (i = 0; i < kPlayers; i++) {
        if (is[i] != 0xFF)
            continue;
        for (j = 0; j < kPlayers; j++)
            if (!taken[j] && same_player(&team->players[i], &base->players[j])) {
                is[i] = (uint8_t)j;
                taken[j] = 1;
                break;
            }
        if (is[i] == 0xFF)
            return 0;
    }
    return 1;
}

/* ---- THE RULE: A CUSTOM TEAM IS 96/97's ----
 *
 * A custom team is made from, and checked against, the original 96/97 data and nothing else,
 * whatever world the game is playing (the owner, 2026-11-04). A world's file for a country
 * replaces 96/97's in what the game reads (src/save_format_json.c), so everything here that
 * makes, reads or checks a custom team runs inside this scope, which reads 96/97: the team
 * database (src/team_db.c keeps one copy a world) and LoadTeamFile (whose cached file goes both
 * ways). World tables stay as they are, so it is not for showing a team list: see edit_slot. */
extern uint8_t _lastLoadedExtNumber;
static int32_t s_baseDepth, s_baseSwitched;
static char    s_baseWorld[8];

static void base_begin(void)
{
    if (s_baseDepth++ == 0 && World_Current() != WORLD_9697) {
        snprintf(s_baseWorld, sizeof s_baseWorld, "%s", World_Tag());
        World_Set(World_TagOf(WORLD_9697));
        _lastLoadedExtNumber = 0xFF;
        s_baseSwitched = 1;
    }
}

static void base_end(void)
{
    if (--s_baseDepth == 0 && s_baseSwitched) {
        s_baseSwitched = 0;
        World_Set(s_baseWorld);
        _lastLoadedExtNumber = 0xFF;
    }
}

/* Names, nationalities or faces not the database team's: a custom team's changes (not its kit:
 * the UNIFORM screen's choice travels inside any team). */
static int32_t renamed(const TeamFileEx *team, const TeamFileEx *base, const uint8_t is[kPlayers])
{
    int32_t i;
    if (strncmp((const char *)team->team.teamName, (const char *)base->team.teamName, sizeof team->team.teamName) != 0 ||
        strncmp((const char *)team->team.coachName, (const char *)base->team.coachName, sizeof team->team.coachName) != 0)
        return 1;
    for (i = 0; i < kPlayers; i++) {
        const PlayerFile *p = &team->players[i], *q = &base->players[is[i]];
        if (strncmp((const char *)p->playerName, (const char *)q->playerName, sizeof p->playerName) != 0 ||
            p->nationality != q->nationality ||
            (p->positionAndFace & PF_FACE_MASK) != (q->positionAndFace & PF_FACE_MASK))
            return 1;
    }
    return 0;
}

int32_t CustomTeam_Allowed(const TeamFileEx *team)
{
    /* The last answer, kept: the lobby asks about the same team several times a second. */
    static TeamFileEx asked;
    static int32_t answer = -1;
    const TeamDbEntry *base;
    uint8_t is[kPlayers];

    if (answer >= 0 && memcmp(&asked, team, sizeof asked) == 0)
        return answer;
    asked = *team;
    /* A 96/97 team, custom or not. */
    base_begin();
    base = TeamDb_Team(team->team.countryNumber, team->team.teamOrdinal);
    answer = base && base->canonical && match_players(team, &base->team, is);
    base_end();
    /* The world's own teams may be played as they are; a change makes one a custom team, and a
     * custom team made from a world's data is what the rule forbids. */
    if (!answer && World_Current() != WORLD_9697) {
        base = TeamDb_Team(team->team.countryNumber, team->team.teamOrdinal);
        answer = base && base->canonical && match_players(team, &base->team, is) && !renamed(team, &base->team, is);
    }
    if (!answer && !g_customTeamQuiet)
        fprintf(stderr, "[custom-team] %.16s (team %d of country %d) is not that team of the database: %s\n",
                (const char *)team->team.teamName, team->team.teamOrdinal, team->team.countryNumber,
                !base ? "the database has no such team" :
                !base->canonical ? "this database's copy of it has been changed" :
                "a player's position, skills or value differ");
    return answer;
}

static int32_t is_custom(const TeamFileEx *team)
{
    const TeamDbEntry *base = TeamDb_Team(team->team.countryNumber, team->team.teamOrdinal);
    const TeamFile *a = &team->team, *b;
    uint8_t is[kPlayers];
    KitLook mine, his;
    int32_t i, kit;

    if (!base || !match_players(team, &base->team, is))
        return 0;                   /* not a database team at all: the screen says what it always did */
    b = &base->team.team;
    if (strncmp((const char *)a->teamName, (const char *)b->teamName, sizeof a->teamName) != 0 ||
        strncmp((const char *)a->coachName, (const char *)b->coachName, sizeof a->coachName) != 0)
        return 1;
    for (kit = 0; kit < 2; kit++) {
        KitArt_FromTeam(a, kit, &mine);
        KitArt_FromTeam(b, kit, &his);
        if (memcmp(&mine, &his, sizeof mine) != 0)
            return 1;
    }
    for (i = 0; i < kPlayers; i++) {
        const PlayerFile *p = &team->players[i], *q = &base->team.players[is[i]];
        if (strncmp((const char *)p->playerName, (const char *)q->playerName, sizeof p->playerName) != 0 ||
            p->nationality != q->nationality ||
            (p->positionAndFace & PF_FACE_MASK) != (q->positionAndFace & PF_FACE_MASK))
            return 1;
    }
    return 0;
}

int32_t CustomTeam_IsCustom(const TeamFileEx *team)
{
    int32_t custom;
    base_begin();
    custom = is_custom(team);
    base_end();
    return custom || (World_Current() != WORLD_9697 && is_custom(team));
}

/* What the server is told: the team as the current database has it. `mustMatch`: only if its
 * players are that team's (0 said, nothing written). */
static int32_t claim(const TeamFileEx *team, uint8_t uuid[16], uint8_t checksum[8], int32_t mustMatch)
{
    const TeamDbEntry *base = TeamDb_Team(team->team.countryNumber, team->team.teamOrdinal);
    char id[TEAM_UUID_TEXT], sum[TEAM_CHECKSUM_TEXT];
    TeamFileEx ordered = *team;
    uint8_t is[kPlayers];
    int32_t i;

    if (mustMatch && !(base && match_players(team, &base->team, is)))
        return 0;
    /* The checksum is of the sixteen in the DATABASE's order, and a custom team may
     * hold them in another: put each back where the database has him. A team whose
     * players are not that team's is hashed as it stands, which is what gives it away. */
    if (base && match_players(team, &base->team, is))
        for (i = 0; i < kPlayers; i++)
            ordered.players[is[i]] = team->players[i];
    if (base)
        snprintf(id, sizeof id, "%s", base->ids.uuid);
    else
        TeamDb_DerivedTeamUuid(team->team.countryNumber, team->team.teamOrdinal, id);
    TeamDb_Checksum(&ordered, sum);
    TeamCheck_Hex(id, uuid, 16);
    TeamCheck_Hex(sum, checksum, 8);
    return 1;
}

void CustomTeam_Claim(const TeamFileEx *team, uint8_t uuid[16], uint8_t checksum[8])
{
    int32_t done;
    base_begin();
    done = claim(team, uuid, checksum, 1);      /* a 96/97 team is told as 96/97's */
    base_end();
    if (!done)
        claim(team, uuid, checksum, 0);
}

/* Which database team a team was made from, judged by its PLAYERS: the number in a
 * record is no guide, since SWOS 2020 gives a custom team a number of its own
 * (PLAYAVELI.TEAM, made from BRAZIL -- country 82, team 2 -- says country 72, team 17,
 * which is GAMESMASTER here). Its own number is tried first, then every team of every
 * file. NULL if no team of the database has those sixteen players. */
static const TeamDbEntry *find_base(const TeamFileEx *team, uint8_t is[kPlayers])
{
    const TeamDbEntry *e = TeamDb_Team(team->team.countryNumber, team->team.teamOrdinal);
    int32_t country, i;

    if (e && e->canonical && match_players(team, &e->team, is))
        return e;
    for (country = 0; country < kCountries; country++)
        for (i = 0; (e = TeamDb_Entry(country, i)) != NULL; i++)
            if (e->team.team.countryNumber == country && e->canonical && match_players(team, &e->team, is))
                return e;
    return NULL;
}

/* ======================================================================
 * CustomTeam: a link to a database team, and what its owner changed
 * ====================================================================== */

static void clean_name(int8_t *to, int32_t size, const char *from)
{
    int32_t n = 0;
    for (; *from && n < size - 1; from++) {
        unsigned char c = (unsigned char)*from;
        if (c >= 'a' && c <= 'z')
            c = (unsigned char)(c - 'a' + 'A');
        if (c >= 0x20)
            to[n++] = (int8_t)c;
    }
    while (n < size)
        to[n++] = 0;
}

static int32_t is_permutation(const uint8_t *order)
{
    uint8_t seen[kPlayers] = { 0 };
    int32_t i;
    for (i = 0; i < kPlayers; i++) {
        if (order[i] >= kPlayers || seen[order[i]])
            return 0;
        seen[order[i]] = 1;
    }
    return 1;
}

/* A team as a CustomTeam: which database team it is (found by its number, then by its
 * players) and everything about it that a custom team may change. 0 if it is no
 * database team's players. */
static int32_t from_team(const TeamFileEx *team, CustomTeam *out)
{
    uint8_t is[kPlayers];
    const TeamDbEntry *base = find_base(team, is);
    char name[32];
    int32_t i;

    if (!base)
        return 0;
    memset(out, 0, sizeof *out);
    snprintf(out->madeFrom, sizeof out->madeFrom, "%s", base->ids.uuid);
    snprintf(out->checksum, sizeof out->checksum, "%s", base->computed);
    snprintf(name, sizeof name, "%.17s", (const char *)team->team.teamName);
    clean_name(out->name, sizeof out->name, name);
    snprintf(name, sizeof name, "%.23s", (const char *)team->team.coachName);
    clean_name(out->coach, sizeof out->coach, name);
    out->tactics = team->team.tactics <= LOBBY_TACTIC_MAX ? team->team.tactics : base->team.team.tactics;
    KitArt_FromTeam(&team->team, 0, &out->kit[0]);      /* brings every value into range */
    KitArt_FromTeam(&team->team, 1, &out->kit[1]);
    memcpy(out->lineup, is_permutation(team->team.playerNumbers) ? team->team.playerNumbers
                                                                 : base->team.team.playerNumbers,
           sizeof out->lineup);
    for (i = 0; i < kPlayers; i++) {
        const PlayerFile *theirs = &team->players[i], *his = &base->team.players[is[i]];
        CustomPlayer *p = &out->players[i];
        uint8_t face = (uint8_t)((theirs->positionAndFace & PF_FACE_MASK) >> PF_FACE_SHIFT);

        snprintf(p->uuid, sizeof p->uuid, "%s", base->ids.player[is[i]]);
        snprintf(name, sizeof name, "%.23s", (const char *)theirs->playerName);
        clean_name(p->name, sizeof p->name, name);
        /* The three faces the game has; a fourth value fits the bits and is nobody's. */
        p->face = face < kFaces ? face : (uint8_t)((his->positionAndFace & PF_FACE_MASK) >> PF_FACE_SHIFT);
        p->nationality = (uint8_t)theirs->nationality < kNationalities ? (uint8_t)theirs->nationality
                                                                       : (uint8_t)his->nationality;
        p->shirtNumber = theirs->shirtNumber >= 1 && theirs->shirtNumber <= kMaxShirtNumber
                       ? theirs->shirtNumber : his->shirtNumber;
    }
    return 1;
}

int32_t CustomTeam_FromTeam(const TeamFileEx *team, CustomTeam *out)
{
    int32_t ok;
    base_begin();
    ok = from_team(team, out);
    base_end();
    return ok;
}

/* The database team as the GAME hands it out (LoadTeamFile: global numbers set), for
 * a team that is going to be played. From a menu only: it loads into the game's own
 * team buffer. */
static int32_t game_team(int32_t country, int32_t ordinal, TeamFileEx *out)
{
    const TeamFileEx *rec = (const TeamFileEx *)(_teamFileBuffer + 2);
    uint16_t count, i;

    if (!TeamDb_Team(country, ordinal))         /* LoadTeamFile panics on a file that is not there */
        return 0;
    if (LoadTeamFile((uint16_t)country) != 0)
        return 0;
    count = *(uint16_t *)_teamFileBuffer;
    for (i = 0; i < count; i++)
        if (rec[i].team.countryNumber == country && rec[i].team.teamOrdinal == ordinal) {
            *out = rec[i];
            return 1;
        }
    return 0;
}

/* The team a CustomTeam plays as: the database team its id names -- which has to be
 * there, be canonical, and be the data the file's checksum says -- with the owner's
 * changes on it. Every position, skill and value comes from the database. */
static int32_t resolve(const CustomTeam *custom, TeamFileEx *out)
{
    const TeamDbEntry *base = TeamDb_ByUuid(custom->madeFrom);
    uint8_t taken[kPlayers] = { 0 };
    TeamFileEx game;
    const char *why = NULL;
    int32_t i, j;

    if (!base)
        why = "this game's database has no team with that id";
    else if (!base->canonical)
        why = "this database's copy of that team has been changed";
    else if (custom->checksum[0] && strcmp(custom->checksum, base->computed) != 0)
        why = "the team with that id here is not the data it was made from";
    else if (!game_team(base->team.team.countryNumber, base->team.team.teamOrdinal, &game))
        why = "the game could not load that team";
    if (why) {
        if (!g_customTeamQuiet)
            fprintf(stderr, "[custom-team] %.16s (made from %s) is not used: %s\n",
                    (const char *)custom->name, custom->madeFrom, why);
        return 0;
    }
    *out = game;
    memcpy(out->team.teamName, custom->name, sizeof out->team.teamName);
    memcpy(out->team.coachName, custom->coach, sizeof out->team.coachName);
    if (custom->tactics <= LOBBY_TACTIC_MAX)
        out->team.tactics = custom->tactics;
    KitArt_ToTeam(&custom->kit[0], 0, &out->team);
    KitArt_ToTeam(&custom->kit[1], 1, &out->team);
    if (is_permutation(custom->lineup))
        memcpy(out->team.playerNumbers, custom->lineup, sizeof out->team.playerNumbers);
    for (i = 0; i < kPlayers; i++) {
        const CustomPlayer *c = &custom->players[i];
        PlayerFile *p = &out->players[i];

        for (j = 0; j < kPlayers; j++)
            if (!taken[j] && strcmp(c->uuid, base->ids.player[j]) == 0)
                break;
        if (j == kPlayers) {
            if (!g_customTeamQuiet)
                fprintf(stderr, "[custom-team] %.16s is not used: its player %d (%s) is not a player of %.16s, "
                                "or is there twice\n", (const char *)custom->name, (int)i + 1, c->uuid,
                        (const char *)base->team.team.teamName);
            return 0;
        }
        taken[j] = 1;
        *p = game.players[j];
        memcpy(p->playerName, c->name, sizeof p->playerName);
        if (c->face < kFaces)
            p->positionAndFace = (uint8_t)((p->positionAndFace & ~PF_FACE_MASK) | (c->face << PF_FACE_SHIFT));
        if (c->nationality < kNationalities)
            p->nationality = (int8_t)c->nationality;
        if (c->shirtNumber >= 1 && c->shirtNumber <= kMaxShirtNumber)
            p->shirtNumber = c->shirtNumber;
    }
    return 1;
}

/* ======================================================================
 * The file: <save folder>/CUSTOMn.TEAM
 *
 * The CustomTeam as JSON. Its field names are the team database's own where the two
 * have the same thing (team_name, coach_name, tactics, the kits, player_numbers,
 * player_name, face, nationality, shirt_number) -- and it has none of the database's
 * others.
 * ====================================================================== */

static void slot_file(int32_t slot, char *name, size_t size)
{
    snprintf(name, size, "CUSTOM%d.TEAM", (int)slot + 1);
}

typedef struct Text { char *at; size_t left; int32_t full; } Text;

static void put(Text *t, const char *format, ...)
{
    va_list args;
    int n;
    va_start(args, format);
    n = vsnprintf(t->at, t->left, format, args);
    va_end(args);
    if (n > 0 && (size_t)n < t->left) {
        t->at += n;
        t->left -= (size_t)n;
    } else if (n > 0) {
        t->full = 1;
    }
}

static void put_string(Text *t, const int8_t *text, int32_t size)
{
    int32_t i;
    put(t, "\"");
    for (i = 0; i < size && text[i]; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\')  put(t, "\\%c", c);
        else if (c < 0x20 || c >= 0x7F) put(t, "\\u%04x", c);
        else put(t, "%c", c);
    }
    put(t, "\"");
}

static void put_kit(Text *t, const char *which, const KitLook *kit)
{
    put(t, "    \"%s_shirt_type\": %d, \"%s_stripes_color\": %d, \"%s_basic_color\": %d, "
           "\"%s_shorts_color\": %d, \"%s_socks_color\": %d,\n",
        which, kit->type, which, kit->stripes, which, kit->basic, which, kit->shorts, which, kit->socks);
}

static int32_t write_json(const CustomTeam *custom, char *json, size_t size)
{
    const TeamDbEntry *base = TeamDb_ByUuid(custom->madeFrom);
    Text t = { json, size, 0 };
    int32_t i, j;

    put(&t, "{\n  \"format\": \"" PROJECT_TEAM_FORMAT "\",\n  \"version\": %d,\n", kFormatVersion);
    put(&t, "  \"made_from\": { \"uuid\": \"%s\", \"checksum\": \"%s\", \"name\": ", custom->madeFrom,
        custom->checksum);
    if (base) put_string(&t, base->team.team.teamName, sizeof base->team.team.teamName);
    else      put(&t, "\"\"");
    put(&t, " },\n  \"team\": {\n    \"team_name\": ");
    put_string(&t, custom->name, sizeof custom->name);
    put(&t, ",\n    \"coach_name\": ");
    put_string(&t, custom->coach, sizeof custom->coach);
    put(&t, ",\n    \"tactics\": %d,\n", custom->tactics);
    put_kit(&t, "primary", &custom->kit[0]);
    put_kit(&t, "secondary", &custom->kit[1]);
    put(&t, "    \"player_numbers\": [");
    for (i = 0; i < kPlayers; i++)
        put(&t, "%s%d", i ? ", " : "", custom->lineup[i]);
    put(&t, "]\n  },\n  \"players\": [\n");
    for (i = 0; i < kPlayers; i++) {
        const CustomPlayer *p = &custom->players[i];
        put(&t, "    { \"uuid\": \"%s\", \"player_name\": ", p->uuid);
        put_string(&t, p->name, sizeof p->name);
        put(&t, ", \"face\": %d, \"nationality\": %d, \"shirt_number\": %d", p->face, p->nationality, p->shirtNumber);
        /* Who he is in the database, for whoever reads the file; nothing reads it back. */
        for (j = 0; base && j < kPlayers; j++)
            if (strcmp(p->uuid, base->ids.player[j]) == 0) {
                put(&t, ", \"was\": ");
                put_string(&t, base->team.players[j].playerName, sizeof base->team.players[j].playerName);
                break;
            }
        put(&t, " }%s\n", i + 1 < kPlayers ? "," : "");
    }
    put(&t, "  ]\n}\n");
    return !t.full;
}

/* mjson hands a \u00XX escape back as UTF-8; the game's names are one byte a letter. */
static void latin1(char *text)
{
    unsigned char *in = (unsigned char *)text, *out = (unsigned char *)text;
    while (*in) {
        if ((*in == 0xC2 || *in == 0xC3) && (in[1] & 0xC0) == 0x80) {
            *out++ = (unsigned char)(((*in & 3) << 6) | (in[1] & 0x3F));
            in += 2;
        } else {
            *out++ = *in++;
        }
    }
    *out = 0;
}

static void json_name(const char *json, int32_t length, const char *path, int8_t *to, int32_t size)
{
    char text[96];
    /* >= 0: an empty name is a name (a blank coach is common in .TEAM files). */
    if (mjson_get_string(json, length, path, text, (int)sizeof text) >= 0) {
        latin1(text);
        clean_name(to, size, text);
    }
}

static int32_t json_int(const char *json, int32_t length, const char *path, int32_t low, int32_t high,
                        int32_t otherwise)
{
    double number;
    return mjson_get_number(json, length, path, &number) && number >= low && number <= high
         ? (int32_t)number : otherwise;
}

static void json_kit(const char *json, int32_t length, const char *which, KitLook *kit)
{
    static const char *const k_fields[5] = { "shirt_type", "stripes_color", "basic_color", "shorts_color",
                                             "socks_color" };
    uint8_t *value[5] = { &kit->type, &kit->stripes, &kit->basic, &kit->shorts, &kit->socks };
    char path[64];
    int32_t i;

    for (i = 0; i < 5; i++) {
        snprintf(path, sizeof path, "$.team.%s_%s", which, k_fields[i]);
        *value[i] = (uint8_t)json_int(json, length, path, 0, i == 0 ? 3 : KIT_COLOURS - 1, *value[i]);
    }
}

/* The file's text as a CustomTeam. Whatever the file leaves out is what the database
 * team has (that is what `out` starts as), so a hand-made file can be as short as its
 * link and one name. 0 for a file that is not one, or links to nothing here. */
static int32_t read_json(const char *json, int32_t length, CustomTeam *out)
{
    const TeamDbEntry *base;
    char uuid[TEAM_UUID_TEXT] = "", path[48];
    int32_t version = json_int(json, length, "$.version", 0, 1000, 0), i;

    if (version != kFormatVersion || mjson_get_string(json, length, "$.made_from.uuid", uuid, (int)sizeof uuid) <= 0) {
        if (!g_customTeamQuiet)
            fprintf(stderr, "[custom-team] the file is not a custom team of this format (version %d, with the id of "
                            "the team it was made from): not used\n", kFormatVersion);
        return 0;
    }
    base = TeamDb_ByUuid(uuid);
    if (!base || !CustomTeam_FromTeam(&base->team, out)) {
        if (!g_customTeamQuiet)
            fprintf(stderr, "[custom-team] the file was made from team %s, which this game's database does not have: "
                            "not used\n", uuid);
        return 0;
    }
    /* The checksum the FILE states, to be held against the database's when it is
     * resolved; a file without one is taken to mean the team as it is here. */
    mjson_get_string(json, length, "$.made_from.checksum", out->checksum, (int)sizeof out->checksum);
    json_name(json, length, "$.team.team_name", out->name, sizeof out->name);
    json_name(json, length, "$.team.coach_name", out->coach, sizeof out->coach);
    out->tactics = (uint8_t)json_int(json, length, "$.team.tactics", 0, LOBBY_TACTIC_MAX, out->tactics);
    json_kit(json, length, "primary", &out->kit[0]);
    json_kit(json, length, "secondary", &out->kit[1]);
    {
        uint8_t order[kPlayers];
        for (i = 0; i < kPlayers; i++) {
            snprintf(path, sizeof path, "$.team.player_numbers[%d]", (int)i);
            order[i] = (uint8_t)json_int(json, length, path, 0, kPlayers - 1, 0xFF);
        }
        if (is_permutation(order))
            memcpy(out->lineup, order, sizeof order);
    }
    for (i = 0; i < kPlayers; i++) {
        CustomPlayer *p = &out->players[i];
        snprintf(path, sizeof path, "$.players[%d].uuid", (int)i);
        if (mjson_get_string(json, length, path, uuid, (int)sizeof uuid) > 0) {
            /* Another of the team's players in this place: he brings his own name and
             * the rest with him, for the file to change in turn. Resolving refuses a
             * player who is not the team's, or is in it twice. */
            int32_t j;
            for (j = 0; j < kPlayers; j++)
                if (strcmp(uuid, base->ids.player[j]) == 0)
                    break;
            if (j < kPlayers && j != i) {
                CustomTeam whole;
                CustomTeam_FromTeam(&base->team, &whole);
                *p = whole.players[j];
            } else if (j == kPlayers) {
                snprintf(p->uuid, sizeof p->uuid, "%s", uuid);
            }
        }
        snprintf(path, sizeof path, "$.players[%d].player_name", (int)i);
        json_name(json, length, path, p->name, sizeof p->name);
        snprintf(path, sizeof path, "$.players[%d].face", (int)i);
        p->face = (uint8_t)json_int(json, length, path, 0, kFaces - 1, p->face);
        snprintf(path, sizeof path, "$.players[%d].nationality", (int)i);
        p->nationality = (uint8_t)json_int(json, length, path, 0, kNationalities - 1, p->nationality);
        snprintf(path, sizeof path, "$.players[%d].shirt_number", (int)i);
        p->shirtNumber = (int8_t)json_int(json, length, path, 1, kMaxShirtNumber, p->shirtNumber);
    }
    return 1;
}

int32_t CustomTeam_Resolve(const CustomTeam *custom, TeamFileEx *out)
{
    int32_t ok;
    base_begin();
    ok = resolve(custom, out);
    base_end();
    return ok;
}

static int32_t slot_save(int32_t slot, const TeamFileEx *team)
{
    static char json[kJsonMax];
    CustomTeam custom;
    const TeamDbEntry *base;
    char name[24];

    if (slot < 0 || slot >= CUSTOM_TEAM_SLOTS)
        return 0;
    if (!CustomTeam_FromTeam(team, &custom) || !write_json(&custom, json, sizeof json)) {
        fprintf(stderr, "[custom-team] %.16s is not a team of the database with only its names, faces, kits and "
                        "line-up changed: not saved\n", (const char *)team->team.teamName);
        return 0;
    }
    slot_file(slot, name, sizeof name);
    if (WriteFile((uint8_t *)name, (uint8_t *)json, (uint32_t)strlen(json)) != 0) {
        fprintf(stderr, "[custom-team] %s could not be written\n", name);
        return 0;
    }
    base = TeamDb_ByUuid(custom.madeFrom);
    fprintf(stderr, "[custom-team] slot %d saved: %.16s, made from %.16s (%s)\n", (int)slot + 1,
            (const char *)custom.name, base ? (const char *)base->team.team.teamName : "?", custom.madeFrom);
    return 1;
}

int32_t CustomTeam_Save(int32_t slot, const TeamFileEx *team)
{
    int32_t ok;
    base_begin();
    ok = slot_save(slot, team);
    base_end();
    return ok;
}

static int32_t slot_load(int32_t slot, TeamFileEx *out)
{
    char name[24];
    uint8_t *data = NULL;
    uint32_t size = 0;
    CustomTeam custom;
    int32_t ok;

    if (slot < 0 || slot >= CUSTOM_TEAM_SLOTS)
        return 0;
    slot_file(slot, name, sizeof name);
    if (swos_io_load_alloc((const uint8_t *)name, &data, &size) != 0)
        return 0;
    if (size == kTeamRecord && data[0] != '{') {
        /* A .TEAM record (SWOS 2020's, an editor's), converted once: the slot becomes
         * JSON, the record is kept beside it. */
        TeamFileEx record;
        memcpy(&record, data, sizeof record);
        ok = CustomTeam_FromTeam(&record, &custom) && CustomTeam_Resolve(&custom, out);
        if (ok) {
            char kept[32];
            snprintf(kept, sizeof kept, "%s.684", name);
            if (swos_io_save((const uint8_t *)kept, data, size) == 0 && CustomTeam_Save(slot, out))
                fprintf(stderr, "[custom-team] %s converted to this game's format; the original is %s\n", name, kept);
        } else {
            fprintf(stderr, "[custom-team] the team file %.16s is not a team of the database: no team here has its "
                            "sixteen players with their positions, skills and values -- not used\n",
                    (const char *)record.team.teamName);
        }
    } else {
        ok = read_json((const char *)data, (int32_t)size, &custom) && CustomTeam_Resolve(&custom, out);
    }
    swos_io_free(data);
    return ok;
}

int32_t CustomTeam_Load(int32_t slot, TeamFileEx *out)
{
    int32_t ok;
    base_begin();
    ok = slot_load(slot, out);
    base_end();
    return ok;
}

/* ======================================================================
 * The editor: the game's EDIT TEAM screen, on a custom team
 * ====================================================================== */

static struct {
    int32_t    on;
    TeamFileEx team;
} s_edit;

int32_t CustomTeam_Editing(void) { return s_edit.on; }
const char *CustomTeam_LineupLabel(void) { return "LINEUP"; }

/* A team of the database, chosen from the game's own list of clubs and national sides. */
static int32_t choose_database_team(TeamFileEx *out)
{
    static char title[] = "SELECT TEAM TO IMPORT";

    ChooseTeamsDialog(1, 1, 0, (uint8_t *)title, (void *)0);
    if ((int16_t)_selectedTeamCountryNumber == -1 || !_selectedTeamPtr)
        return 0;
    *out = *_selectedTeamPtr;
    return 1;
}

/* What the slots screen has to say about the last edit (the rule, above). */
static const char *s_note1 = "", *s_note2 = "";

static void edit_slot(int32_t slot)
{
    TeamFileEx keep = _selectedTeams[0];

    s_note1 = s_note2 = "";
    if (!CustomTeam_Load(slot, &s_edit.team)) {
        /* The team list shows the world being played, and its tables stay that world's: nothing
         * in it may become a custom team. */
        if (World_Current() != WORLD_9697) {
            s_note1 = "CUSTOM TEAMS ARE MADE FROM 96/97 TEAMS ONLY.";
            s_note2 = "SWITCH TO 96/97 TO CREATE ONE.";
            return;
        }
        if (!choose_database_team(&s_edit.team))
            return;
        CustomTeam_Save(slot, &s_edit.team);
    }
    s_edit.on = 1;
    EditTeamsCallback(&s_edit.team);        /* the EDIT TEAM screen; OK writes the team back */
    s_edit.on = 0;
    _selectedTeams[0] = keep;
    if (!CustomTeam_Save(slot, &s_edit.team)) {         /* (its IMPORT, from a world's list) */
        s_note1 = "NOT SAVED: NOT A TEAM OF THE 96/97 DATA.";
        s_note2 = "CUSTOM TEAMS ARE MADE FROM 96/97 TEAMS ONLY.";
    }
}

/* LINEUP: the team screen a match shows, on the team being edited (which the EDIT TEAM
 * screen keeps in _selectedTeams[0]). CONFIRM keeps the line-up and formation, EXIT
 * drops what was changed there. The same arrangement the online lobby makes around
 * that screen (online_menus.c lineup_begin). */
void CustomTeam_Lineup(void)
{
    static char confirm[] = "CONFIRM";
    TeamFileEx before = _selectedTeams[0], other = _selectedTeams[1];
    uint16_t numSel = _numSelectedTeams, curNo = _currentTeamNumber, selNo = _selTeamNumber;
    uint16_t gameType = _gameType, isCareer = _isCareer, training = _trainingGame;
    extern uint16_t _twoPlayers, _team1SelectedPlayerIndex, _team2SelectedPlayerIndex;
    uint16_t twoPlayers = _twoPlayers;
    uint16_t marked1 = _team1SelectedPlayerIndex, marked2 = _team2SelectedPlayerIndex;

    /* Nobody is marked as the player to play as: see lineup_begin (ReportBot 0022, the
     * goalkeeper's row blinking). */
    _team1SelectedPlayerIndex = 0xFFFF;
    _team2SelectedPlayerIndex = 0xFFFF;

    /* One player sits here: see lineup_begin (online_menus.c) for why a _twoPlayers left
     * over from the last match took the controller off this screen (ReportBot 0020). */
    _twoPlayers = 0;
    _selectedTeams[0].team.teamControls = kPlayerCoach;
    _selectedTeams[1] = _selectedTeams[0];
    _numSelectedTeams = 2;
    _currentTeamNumber = 0xFFFF;
    _selTeamNumber = 0xFFFF;
    _gameType = 0;
    _isCareer = 0;
    _trainingGame = 0;
    _exitGameFlag = 0;
    g_playMatchStartLabel = confirm;
    g_playMatchNoUniform = 1;               /* the kits are this editor's own business */
    ShowPlayMatchMenu(1, 5, 0, 1, 1, &_selectedTeams[0], &_selectedTeams[1]);
    g_playMatchNoUniform = 0;
    g_playMatchStartLabel = NULL;

    if (_exitGameFlag)
        _selectedTeams[0] = before;
    else
        _selectedTeams[0].team.teamControls = before.team.teamControls;
    _exitGameFlag = 0;
    _selectedTeams[1] = other;
    _numSelectedTeams = numSel; _currentTeamNumber = curNo; _selTeamNumber = selNo;
    _gameType = gameType; _isCareer = isCareer; _trainingGame = training;
    _twoPlayers = twoPlayers;
    _team1SelectedPlayerIndex = marked1;
    _team2SelectedPlayerIndex = marked2;
}

/* ======================================================================
 * MY CUSTOM TEAMS: the three slots (swos-2020 selectMyCustomTeams.mnu)
 * ====================================================================== */

static struct {
    int32_t picking;                /* choosing a team for a match, not editing one */
    int32_t chosen;                 /* the slot fired on, -1 = EXIT */
    int32_t filled[CUSTOM_TEAM_SLOTS];
    char    label[CUSTOM_TEAM_SLOTS][40];
} s_slots;

static void read_slots(void)
{
    TeamFileEx team;
    int32_t slot;

    for (slot = 0; slot < CUSTOM_TEAM_SLOTS; slot++) {
        s_slots.filled[slot] = CustomTeam_Load(slot, &team);
        if (s_slots.filled[slot])
            snprintf(s_slots.label[slot], sizeof s_slots.label[slot], "%.16s", (const char *)team.team.teamName);
        else
            snprintf(s_slots.label[slot], sizeof s_slots.label[slot], "%s",
                     s_slots.picking ? "-" : "EMPTY SLOT");
    }
}

static void selectMyCustomTeamsMenu_OnInit(void) { Account_TeamImportReset(); }

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
static int32_t import_draw(void)
{
    extern uint8_t _currentMenu[];
    static const int32_t all[5] = { SelectMyCustomTeams_importTeams, SelectMyCustomTeams_importYes,
                                    SelectMyCustomTeams_importNo, SelectMyCustomTeams_importLine1,
                                    SelectMyCustomTeams_importLine2 };
    static char label[] = "IMPORT FROM SWOS 2020", busy[] = "CHECKING SWOS 2020...", yes[] = "YES", no[] = "NO";
    const int32_t on = !s_slots.picking && Account_Has(ACCOUNT_HAS_TEAM_IMPORT);
    Menu *menu = (Menu *)_currentMenu;
    const char *line1 = "", *line2 = "";
    int32_t state = TEAM_IMPORT_IDLE, changed = 0, i;

    if (on)
        state = Account_TeamImportPoll(&line1, &line2, &changed);
    if (changed)
        read_slots();
    if (s_note1[0] && !line1[0]) {
        line1 = s_note1;
        line2 = s_note2;
    }
    for (i = 0; i < 5; i++)
        CalcMenuEntryAddress((uint16_t)all[i])->isInvisible = !on && !(i >= 3 && s_note1[0]);
    CalcMenuEntryAddress(SelectMyCustomTeams_importTeams)->isInvisible = !on || state == TEAM_IMPORT_ASKING;
    CalcMenuEntryAddress(SelectMyCustomTeams_importYes)->isInvisible = state != TEAM_IMPORT_ASKING;
    CalcMenuEntryAddress(SelectMyCustomTeams_importNo)->isInvisible = state != TEAM_IMPORT_ASKING;
    CalcMenuEntryAddress(SelectMyCustomTeams_importTeams)->fg = (uintptr_t)(state == TEAM_IMPORT_BUSY ? busy : label);
    CalcMenuEntryAddress(SelectMyCustomTeams_importTeams)->bgColor = state == TEAM_IMPORT_BUSY ? 7 : 14;
    CalcMenuEntryAddress(SelectMyCustomTeams_importYes)->fg = (uintptr_t)yes;
    CalcMenuEntryAddress(SelectMyCustomTeams_importNo)->fg = (uintptr_t)no;
    CalcMenuEntryAddress(SelectMyCustomTeams_importLine1)->fg = (uintptr_t)line1;
    CalcMenuEntryAddress(SelectMyCustomTeams_importLine2)->fg = (uintptr_t)line2;
    CalcMenuEntryAddress(SelectMyCustomTeams_importLine1)->isDisabled = 1;
    CalcMenuEntryAddress(SelectMyCustomTeams_importLine2)->isDisabled = 1;

    if (state == TEAM_IMPORT_ASKING) {
        slots_link(SelectMyCustomTeams_importYes, -1, SelectMyCustomTeams_importNo, -1, SelectMyCustomTeams_exit);
        slots_link(SelectMyCustomTeams_importNo, SelectMyCustomTeams_importYes, -1, -1, SelectMyCustomTeams_exit);
        slots_link(SelectMyCustomTeams_exit, -1, -1, SelectMyCustomTeams_importNo, -1);
        if (menu->selectedEntry == CalcMenuEntryAddress(SelectMyCustomTeams_importTeams))
            menu->selectedEntry = CalcMenuEntryAddress(SelectMyCustomTeams_importNo);   /* (nothing lost by default) */
    } else if (on) {
        slots_link(SelectMyCustomTeams_team3, -1, -1, SelectMyCustomTeams_team2, SelectMyCustomTeams_importTeams);
        slots_link(SelectMyCustomTeams_importTeams, -1, -1, SelectMyCustomTeams_team3, SelectMyCustomTeams_exit);
        slots_link(SelectMyCustomTeams_exit, -1, -1, SelectMyCustomTeams_importTeams, -1);
        if (menu->selectedEntry == CalcMenuEntryAddress(SelectMyCustomTeams_importYes) ||
            menu->selectedEntry == CalcMenuEntryAddress(SelectMyCustomTeams_importNo))
            menu->selectedEntry = CalcMenuEntryAddress(SelectMyCustomTeams_importTeams);
    }
    return state != TEAM_IMPORT_IDLE;
}

static void selectMyCustomTeamsImport(void)    { Account_TeamImportStart(); }
static void selectMyCustomTeamsImportYes(void) { Account_TeamImportAnswer(1); }
static void selectMyCustomTeamsImportNo(void)  { Account_TeamImportAnswer(0); }

static void selectMyCustomTeamsMenu_OnDraw(void)
{
    static char pickHeader[] = "CHOOSE YOUR TEAM", pickExit[] = "OTHER TEAMS";
    const int32_t importing = import_draw();    /* (the slots wait while it decides what goes in them) */
    int32_t slot;

    for (slot = 0; slot < CUSTOM_TEAM_SLOTS; slot++) {
        MenuEntry *e = CalcMenuEntryAddress((uint16_t)(SelectMyCustomTeams_team1 + slot));
        e->fg = (uintptr_t)s_slots.label[slot];
        e->bgColor = s_slots.filled[slot] ? 13 : 7;     /* light blue: a team; grey: none */
        e->isDisabled = importing || (s_slots.picking && !s_slots.filled[slot]);
    }
    if (s_slots.picking) {
        CalcMenuEntryAddress(SelectMyCustomTeams_header)->fg = (uintptr_t)pickHeader;
        CalcMenuEntryAddress(SelectMyCustomTeams_exit)->fg = (uintptr_t)pickExit;
    }
}

static void select_slot(int32_t slot)
{
    if ((s_slots.picking && !s_slots.filled[slot]) || CalcMenuEntryAddress((uint16_t)(SelectMyCustomTeams_team1 + slot))->isDisabled)
        return;
    s_slots.chosen = slot;
    SetExitMenuFlag();
}

static void selectMyCustomTeam1(void) { select_slot(0); }
static void selectMyCustomTeam2(void) { select_slot(1); }
static void selectMyCustomTeam3(void) { select_slot(2); }

#define HANDLER(fn) { #fn, (void *)fn, SYM_FUNC }
static const MenuSymbol k_handlers[] = {
    HANDLER(selectMyCustomTeamsMenu_OnInit),
    HANDLER(selectMyCustomTeamsMenu_OnDraw),
    HANDLER(selectMyCustomTeam1),
    HANDLER(selectMyCustomTeam2),
    HANDLER(selectMyCustomTeam3),
    HANDLER(selectMyCustomTeamsImport),
    HANDLER(selectMyCustomTeamsImportYes),
    HANDLER(selectMyCustomTeamsImportNo),
};
#undef HANDLER

static int32_t show_slots(int32_t picking)
{
    static int32_t registered;
    uint8_t *menu;

    if (!registered) {
        registered = 1;
        MenuJson_RegisterSymbols(k_handlers, (int)(sizeof k_handlers / sizeof k_handlers[0]));
    }
    menu = MenuJson_Load("swos2020/selectMyCustomTeams");
    if (!menu)
        return -1;
    s_slots.picking = picking;
    s_slots.chosen = -1;
    read_slots();
    ShowMenu(menu);
    return s_slots.chosen;
}

void CustomTeam_Manage(void)
{
    int32_t slot;
    while ((slot = show_slots(0)) >= 0)
        edit_slot(slot);
}

int32_t CustomTeam_Pick(TeamFileEx *out)
{
    TeamFileEx team;
    int32_t slot, any = 0;

    for (slot = 0; slot < CUSTOM_TEAM_SLOTS; slot++)
        any |= CustomTeam_Load(slot, &team);
    if (!any)
        return -1;
    slot = show_slots(1);
    if (slot < 0)
        return 0;
    return CustomTeam_Load(slot, out) ? 1 : 0;
}

/* ======================================================================
 * --custom-team-selftest
 *
 * The ids first (TeamDb_SelfTest). Then every team of the database: allowed as it is;
 * refused with one skill raised; and, renamed, redressed and rearranged, the same team
 * after a trip through the file's text -- which has no skill in it, refuses a wrong
 * checksum and an unknown id, and ignores a skill someone writes into it.
 * ====================================================================== */
/* The rule, with a world selected (SWOS_WORLD=2627 --custom-team-selftest): a team of the world
 * that is not a 96/97 team cannot become a custom team, may be played as it is, and is refused
 * renamed. */
static int32_t world_self_test(void)
{
    int32_t country, i, worldOnly = 0, bad = 0;

    if (World_Current() == WORLD_9697)
        return 1;
    g_customTeamQuiet = 1;
    for (country = 0; country < kCountries; country++) {
        const TeamDbEntry *e;
        for (i = 0; (e = TeamDb_Entry(country, i)) != NULL; i++) {
            TeamFileEx team = e->team;
            CustomTeam custom;
            if (e->team.team.countryNumber != country || !e->canonical || CustomTeam_FromTeam(&team, &custom))
                continue;
            worldOnly++;
            if (!CustomTeam_Allowed(&team))
                bad++;
            memcpy(team.team.teamName, "NOT 96/97 DATA", 15);
            if (CustomTeam_Allowed(&team))
                bad++;
        }
    }
    g_customTeamQuiet = 0;
    fprintf(stderr, "[custom-team] the rule, in world %s: %d teams not of the 96/97 data, none can be a custom "
                    "team; %d wrong answers\n", World_Tag(), (int)worldOnly, (int)bad);
    return worldOnly > 0 && bad == 0;   /* (none at all: the world's data was not there to test) */
}

static int32_t self_test(void);

int32_t CustomTeam_SelfTest(void)
{
    int32_t ok;
    base_begin();
    ok = self_test();
    base_end();
    return ok && world_self_test();
}

static int32_t self_test(void)
{
    static char json[kJsonMax], edited[kJsonMax + 64];
    int32_t country, i, teamsSeen = 0, failed = 0, idsOk = TeamDb_SelfTest();
    const TeamDbEntry *e;

    for (country = 0; country < kCountries; country++) {
        for (i = 0; (e = TeamDb_Entry(country, i)) != NULL; i++) {
            TeamFileEx team = e->team, back, cheat, want, base;
            CustomTeam custom, read;
            uint8_t swap;
            int32_t bad = 0;
            char *at;

            if (team.team.countryNumber != country)
                continue;                           /* a record filed under another country's number */
            teamsSeen++;
            if (!CustomTeam_Allowed(&team))
                bad |= 1;
            cheat = team;
            cheat.players[3].speedFinishing = (int8_t)(cheat.players[3].speedFinishing ^ 0x10);
            g_customTeamQuiet = 1;
            if (CustomTeam_Allowed(&cheat))
                bad |= 2;
            g_customTeamQuiet = 0;

            /* What an owner may do: rename, a face, both kits, the formation, two
             * players swapped in the line-up and two swapped in the record itself. */
            clean_name(team.team.teamName, sizeof team.team.teamName, "SELF TEST UNITED");
            clean_name(team.players[5].playerName, sizeof team.players[5].playerName, "A \"QUOTED\" NAME");
            team.players[5].positionAndFace = (uint8_t)((team.players[5].positionAndFace & ~PF_FACE_MASK) |
                ((((team.players[5].positionAndFace & PF_FACE_MASK) >> PF_FACE_SHIFT) + 1) % kFaces) << PF_FACE_SHIFT);
            team.team.prShirtType = 3; team.team.prStripesColor = 9; team.team.secSocksColor = 4;
            team.team.tactics = 5;
            swap = team.team.playerNumbers[1];
            team.team.playerNumbers[1] = team.team.playerNumbers[12];
            team.team.playerNumbers[12] = swap;
            { PlayerFile p = team.players[7]; team.players[7] = team.players[9]; team.players[9] = p; }
            /* What the team screen does to a starter and a substitute it swaps, and what
             * a SWOS 2020 file does to a player: a number and a nationality of his own. */
            { int8_t n = team.players[2].shirtNumber;
              team.players[2].shirtNumber = team.players[13].shirtNumber; team.players[13].shirtNumber = n; }
            team.players[4].nationality = 13;
            team.players[4].shirtNumber = 77;
            if (!CustomTeam_Allowed(&team))
                bad |= 4;

            if (!CustomTeam_FromTeam(&team, &custom) || !write_json(&custom, json, sizeof json) ||
                !read_json(json, (int32_t)strlen(json), &read) || !CustomTeam_Resolve(&read, &back) ||
                !CustomTeam_Resolve(&custom, &want)) {
                bad |= 8;
            } else {
                if (memcmp(&read, &custom, sizeof custom) != 0)
                    bad |= 16;                      /* the text did not keep the CustomTeam */
                if (memcmp(&back, &want, sizeof want) != 0 || !CustomTeam_Allowed(&back))
                    bad |= 32;
                /* It is the database's players under the owner's changes. */
                if (!game_team(country, team.team.teamOrdinal, &base) ||
                    memcmp(want.team.teamName, "SELF TEST UNITED", 16) != 0 || want.team.tactics != 5 ||
                    want.team.prShirtType != 3 || want.team.secSocksColor != 4 ||
                    want.team.playerNumbers[1] != team.team.playerNumbers[1] ||
                    want.players[4].nationality != 13 || want.players[4].shirtNumber != 77 ||
                    memcmp(want.players[5].playerName, "A \"QUOTED\" NAME", 15) != 0 ||
                    !same_player(&want.players[7], &team.players[7]) ||
                    !same_player(&want.players[9], &team.players[9]))
                    bad |= 64;
                if (strstr(json, "passing") || strstr(json, "price") || strstr(json, "position"))
                    bad |= 128;                     /* the file has player data in it */

                g_customTeamQuiet = 1;
                /* A checksum that is not the database's: the team with that id here is
                 * not the data the file was made from. */
                read = custom;
                read.checksum[0] = read.checksum[0] == '0' ? '1' : '0';
                if (CustomTeam_Resolve(&read, &back))
                    bad |= 256;
                /* An id the database does not have. */
                read = custom;
                read.madeFrom[0] = read.madeFrom[0] == '0' ? '1' : '0';
                if (CustomTeam_Resolve(&read, &back))
                    bad |= 512;
                /* A player of another team. */
                read = custom;
                read.players[6].uuid[0] = read.players[6].uuid[0] == '0' ? '1' : '0';
                if (CustomTeam_Resolve(&read, &back))
                    bad |= 1024;
                g_customTeamQuiet = 0;
                /* A skill written into the file by hand is not read: the player is the
                 * database's all the same. */
                at = strstr(json, "\"player_name\"");
                if (at) {
                    snprintf(edited, sizeof edited, "%.*s\"passing\": 15, \"speed\": 15, \"price\": 49, %s",
                             (int)(at - json), json, at);
                    if (!read_json(edited, (int32_t)strlen(edited), &read) || !CustomTeam_Resolve(&read, &back) ||
                        memcmp(&back, &want, sizeof want) != 0)
                        bad |= 2048;
                }
            }
            if (bad) {
                failed++;
                fprintf(stderr, "[custom-team] self-test: country %d team %d (%.16s) failed, code %d\n",
                        (int)country, e->team.team.teamOrdinal, (const char *)e->team.team.teamName, (int)bad);
            }
        }
    }
    fprintf(stderr, "[custom-team] self-test: %d teams of the database, %d failed\n", (int)teamsSeen, (int)failed);
    return idsOk && teamsSeen > 0 && failed == 0;
}

/* ======================================================================
 * --convert-teams=<in>,<out>
 *
 * Every *.TEAM record in <in> through the conversion a slot gets, written to <out>
 * under the same name -- like a career, a custom team keeps its extension and holds
 * JSON -- and read back to prove it. A record that is not a database team (its players
 * are no team's) cannot be linked to one, so there is nothing to write: the log names
 * the database team it comes closest to and how many of its sixteen players match.
 * ====================================================================== */
static int32_t closest_team(const TeamFileEx *team, const TeamDbEntry **nearest)
{
    const TeamDbEntry *e;
    int32_t country, i, best = -1;

    *nearest = NULL;
    for (country = 0; country < kCountries; country++)
        for (i = 0; (e = TeamDb_Entry(country, i)) != NULL; i++) {
            uint8_t taken[kPlayers] = { 0 };
            int32_t p, q, same = 0;
            if (e->team.team.countryNumber != country)
                continue;
            for (p = 0; p < kPlayers; p++)
                for (q = 0; q < kPlayers; q++)
                    if (!taken[q] && same_player(&team->players[p], &e->team.players[q])) {
                        taken[q] = 1;
                        same++;
                        break;
                    }
            if (same > best) {
                best = same;
                *nearest = e;
            }
        }
    return best;
}

static int32_t convert_dir(const char *in, const char *out);

int32_t CustomTeam_ConvertDir(const char *in, const char *out)
{
    int32_t ok;
    base_begin();
    ok = convert_dir(in, out);
    base_end();
    return ok;
}

static int32_t convert_dir(const char *in, const char *out)
{
    static char json[kJsonMax];
    struct dirent *de;
    DIR *dir = opendir(in);
    int32_t files = 0, converted = 0, foreign = 0, unreadable = 0, mismatched = 0;
    int32_t byMatched[kPlayers + 1] = { 0 };

    if (!dir) {
        fprintf(stderr, "[convert-teams] cannot open %s\n", in);
        return 0;
    }
    while ((de = readdir(dir)) != NULL) {
        char path[1024], target[1024], stem[256];
        uint8_t bytes[kTeamRecord + 1];
        TeamFileEx record, team, back;
        CustomTeam custom, read;
        size_t len = strlen(de->d_name), got;
        FILE *f;

        if (len < 6 || strcasecmp(de->d_name + len - 5, ".TEAM") != 0)
            continue;
        files++;
        snprintf(path, sizeof path, "%s/%s", in, de->d_name);
        f = fopen(path, "rb");
        got = f ? fread(bytes, 1, sizeof bytes, f) : 0;
        if (f)
            fclose(f);
        if (got != kTeamRecord) {
            fprintf(stderr, "[convert-teams] %s: not a %d-byte team record (%d bytes)\n", de->d_name,
                    kTeamRecord, (int)got);
            unreadable++;
            continue;
        }
        memcpy(&record, bytes, sizeof record);
        if (!CustomTeam_FromTeam(&record, &custom) || !CustomTeam_Resolve(&custom, &team) ||
            !write_json(&custom, json, sizeof json)) {
            const TeamDbEntry *nearest;
            int32_t same = closest_team(&record, &nearest);
            foreign++;
            byMatched[same < 0 ? 0 : same]++;
            fprintf(stderr, "[convert-teams] %s (%.16s): NOT A DATABASE TEAM, not written -- closest is %.16s, "
                            "%d of 16 players match\n", de->d_name, (const char *)record.team.teamName,
                    nearest ? (const char *)nearest->team.team.teamName : "-", (int)same);
            continue;
        }
        snprintf(stem, sizeof stem, "%.*s", (int)(len - 5), de->d_name);
        snprintf(target, sizeof target, "%s/%s.TEAM", out, stem);   /* the same name, our format inside */
        f = fopen(target, "wb");
        if (!f || fwrite(json, 1, strlen(json), f) != strlen(json)) {
            fprintf(stderr, "[convert-teams] %s: cannot write %s\n", de->d_name, target);
            if (f)
                fclose(f);
            unreadable++;
            continue;
        }
        fclose(f);
        /* What was written has to load as the team it was written from. */
        if (!read_json(json, (int32_t)strlen(json), &read) || !CustomTeam_Resolve(&read, &back) ||
            memcmp(&back, &team, sizeof team) != 0) {
            fprintf(stderr, "[convert-teams] %s: written, but does not load back as the same team\n", de->d_name);
            mismatched++;
        }
        converted++;
    }
    closedir(dir);
    fprintf(stderr, "[convert-teams] %d files: %d converted, %d not database teams (not written), "
                    "%d not loading back the same, %d unreadable\n",
            (int)files, (int)converted, (int)foreign, (int)mismatched, (int)unreadable);
    if (foreign) {
        int32_t n;
        fprintf(stderr, "[convert-teams] not database teams, by how many of 16 players match the closest one:");
        for (n = kPlayers; n >= 0; n--)
            if (byMatched[n])
                fprintf(stderr, " %d->%d", (int)n, (int)byMatched[n]);
        fprintf(stderr, "\n");
    }
    return files > 0 && unreadable == 0 && mismatched == 0;
}
