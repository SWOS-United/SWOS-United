/*
 * online_account_united.c -- SWOS United's accounts (include/online_account.h): the game's
 * Account_* answered with SWOS-API's client (third_party/swos-api: United_LoginBegin/Poll and
 * the session in its client/src/united_api.c). A player logs in to their sensiblesoccer.de
 * account, as in SWOS 2020: a POST of the username and password over HTTPS with the
 * certificate checked. The reply is "<user id>;<stream key>" for a good account.
 *
 * With [netplay] account_site = swosunited the log-in goes to swos.swosunited.com instead:
 * POST api/auth/key (username, password) answers {"status":"ok","user_id","api_key",...}, or
 * 401 invalid_credentials / 429 too_many_attempts (SWOS-API issue 2). The account id is the
 * same as sensiblesoccer.de's, so ranked and league results still go there. The key is kept in
 * memory for this session; each log-in makes a new one and ends the last.
 */
/* SDL builds only: the accounts, the rankings and the leagues are SWOS United's online play,
 * which the Amiga build does not have (its online menus are SDL-only too: Makefile.amiga
 * SDL_ONLY_SRCS). Guarded here, in United's own file, so no shared file changes. */
#ifdef SWOS_SDL_BUILD

#include "online_account.h"
#include "project_identity.h"
#include "united_api.h"
#include "united_ui.h"            /* the screens Account_Init registers */

#if PROJECT_ACCOUNTS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(SWOS_SDL_BUILD) && defined(SWOS_HTTPS)
#include <SDL.h>
#include "https_client.h"
#endif

static const char *const kUrls[ACCOUNT_LINKS] = {
    "https://www.sensiblesoccer.de/registration",               /* REGISTER */
    "https://sensiblesoccer.de/registration?view=reset",        /* MY PASSWORD? */
    "https://www.sensiblesoccer.de/registration?view=remind",   /* MY USERNAME? */
    "https://www.sensiblesoccer.de/online-leagues/asl",         /* ONLINE LEAGUES */
    "https://discord.gg/Vct4TuGyaJ",                            /* JOIN DISCORD TO APPLY */
};

static void test_login(void);

/* ---- SWOS 2020's ban list ----
 *
 * Its BanUsers: "NAME, MODE, *" a line, ended by "- END OF SETTING -", read at each log-in, names in
 * capitals. As in SWOS 2020, a list that does not come bans nobody: the site being down locks
 * no one out. SWOS_UNITED_BANS_TEST="NAME, MODE;..." adds lines, for a test. */
#if defined(SWOS_SDL_BUILD) && defined(SWOS_HTTPS)
enum { MAX_BANS = 512 };
static struct { char name[40], mode[24]; } s_bans[MAX_BANS];
static int32_t s_banCount;

static void trim(char *s)
{
    char *p = s;
    size_t n;
    while (*p == ' ' || *p == '\t')
        p++;
    memmove(s, p, strlen(p) + 1);
    n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n'))
        s[--n] = 0;
}

static void ban_line(const char *line)
{
    char copy[128], *comma, *end;
    snprintf(copy, sizeof copy, "%s", line);
    if (s_banCount == MAX_BANS || !(comma = strchr(copy, ',')))
        return;
    *comma++ = 0;
    if ((end = strchr(comma, ',')) != NULL)
        *end = 0;
    trim(copy);
    trim(comma);
    if (!copy[0] || !comma[0])
        return;
    snprintf(s_bans[s_banCount].name, sizeof s_bans[0].name, "%s", copy);
    snprintf(s_bans[s_banCount].mode, sizeof s_bans[0].mode, "%s", comma);
    s_banCount++;
}

static void ban_lines(const char *text, char separator)
{
    while (text && *text) {
        const char *next = strchr(text, separator);
        const size_t n = next ? (size_t)(next - text) : strlen(text);
        char line[128];
        snprintf(line, sizeof line, "%.*s", (int)(n < sizeof line ? n : sizeof line - 1), text);
        ban_line(line);
        text = next ? next + 1 : NULL;
    }
}

int32_t United_Banned(const char *name, const char *mode)
{
    int32_t i;
    for (i = 0; name && i < s_banCount; i++)
        if (strcmp(s_bans[i].mode, mode) == 0 && SDL_strcasecmp(s_bans[i].name, name) == 0)
            return 1;
    return 0;
}
#else
int32_t United_Banned(const char *name, const char *mode) { (void)name; (void)mode; return 0; }
#endif

void Account_Init(void)
{
    /* Which site the accounts use: [mod.menus2020] account_site = sensiblesoccer (the default) |
     * swosunited (was [netplay] account_site). */
    {
        extern const char *ModApi_Setting(const char *slug, const char *key, const char *fallback);
        United_SetSite(ModApi_Setting("menus2020", "account_site", "sensiblesoccer"));
    }
    UnitedRankings_Register();
    UnitedLeagues_Register();
    UnitedMatches_Register();
    UnitedMatchInfo_Register();
    test_login();
}

int32_t Account_Has(AccountFeature feature)
{
    return feature == ACCOUNT_HAS_RANKINGS || feature == ACCOUNT_HAS_RANKED ||
           feature == ACCOUNT_HAS_LEAGUES || feature == ACCOUNT_HAS_TEAM_IMPORT;      /* (MATCH LOGS: to come) */
}

int32_t     Account_Enabled(void)         { return 1; }
const char *Account_Prompt(void)
{
    return United_NewSite() ? "LOGIN TO YOUR SWOS UNITED ACCOUNT" : "LOGIN TO YOUR SENSIBLESOCCER.DE ACCOUNT";
}
const char *Account_Url(AccountLink link) { return (uint32_t)link < ACCOUNT_LINKS ? kUrls[link] : 0; }
const char *Account_Error(void)           { return United_LoginError(); }
void        Account_Begin(const char *user, const char *password) { United_LoginBegin(user, password); }

/* The log-in (SWOS-API's), then the ban list: a player banned from ALL LOG IN is refused
 * (United_LoginRefuse forgets the session). No list (no answer): nobody is banned this session.
 * SWOS_UNITED_BANS_TEST="NAME, MODE;..." adds lines, for a test. */
int32_t Account_Poll(void)
{
#if defined(SWOS_SDL_BUILD) && defined(SWOS_HTTPS)
    static int32_t s_banStage;          /* 1: the list is being fetched */
    if (s_banStage) {
        HttpsReply reply;
        if (!Https_PollAsync(&reply))
            return 0;
        s_banStage = 0;
        s_banCount = 0;
        if (reply.status == 200 && reply.body)
            ban_lines(reply.body, '\n');
        else
            fprintf(stderr, "[account] no ban list (%d): nobody is banned this session\n", (int)reply.status);
        Https_Free(&reply);
        ban_lines(getenv("SWOS_UNITED_BANS_TEST"), ';');
        fprintf(stderr, "[account] ban list: %d lines\n", (int)s_banCount);
        if (United_Banned(United_UserName(), "ALL LOG IN")) {
            fprintf(stderr, "[account] %s is banned from all log in\n", United_UserName());
            United_LoginRefuse("BANNED FROM ALL LOG IN. CONTACT THE SITE MANAGER.");
            return -1;
        }
        return 1;
    }
    {
        const int32_t r = United_LoginPoll();
        if (r != 1)
            return r;
        if (Https_RequestAsync(PROJECT_BAN_LIST, NULL) == 0) {
            s_banStage = 1;
            return 0;
        }
        return 1;                       /* (no request: nobody is banned this session) */
    }
#else
    return United_LoginPoll();
#endif
}

/* A test drives the matchmaking menu with no hands (SWOS_MM_AUTO), so nothing types a
 * password: SWOS_UNITED_LOGIN=<user> logs in as that user with the password in
 * SWOS_UNITED_ACCOUNTS (lines of USER:PASSWORD -- the repo's gitignored .test-accounts),
 * before the menus open. Neither the password nor the file's contents are logged. */
#if defined(SWOS_SDL_BUILD) && defined(SWOS_HTTPS)
static void test_login(void)
{
    const char *user = getenv("SWOS_UNITED_LOGIN"), *file = getenv("SWOS_UNITED_ACCOUNTS");
    char line[128];
    FILE *f;
    int32_t r;
    if (!user || !file || !(f = fopen(file, "r")))
        return;
    while (fgets(line, sizeof line, f)) {
        char *colon = strchr(line, ':'), *nl;
        if (!colon)
            continue;
        *colon = 0;
        if ((nl = strpbrk(colon + 1, "\r\n")) != NULL)
            *nl = 0;
        if (strcmp(line, user) != 0)
            continue;
        Account_Begin(line, colon + 1);
        memset(line, 0, sizeof line);
        while ((r = Account_Poll()) == 0)
            SDL_Delay(10);
        fprintf(stderr, "[account] test login as %s: %s\n", United_UserName(), r > 0 ? "good" : United_LoginError());
        break;
    }
    fclose(f);
}
#else
static void test_login(void) {}
#endif

#endif /* PROJECT_ACCOUNTS */

#endif /* SWOS_SDL_BUILD */
