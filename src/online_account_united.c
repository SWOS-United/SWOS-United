/*
 * online_account_united.c -- SWOS United's accounts (include/online_account.h): a player logs
 * in to their sensiblesoccer.de account, as in SWOS 2020 (its PHPManager::logIn, now in
 * third_party/swos-api): a POST of the username and password to the site's API, over HTTPS
 * with its certificate checked (src/https_client.c). The reply is "<user id>;<stream key>" for a
 * good account, anything else for a bad one.
 *
 * With [netplay] account_site = swosunited the log-in goes to swos.swosunited.com instead:
 * POST api/auth/key (username, password) answers {"status":"ok","user_id","api_key",...}, or
 * 401 invalid_credentials / 429 too_many_attempts (SWOS-API issue 2). The account id is the
 * same as sensiblesoccer.de's, so ranked and league results still go there. The key is kept in
 * memory for this session; each log-in makes a new one and ends the last.
 */
#include "online_account.h"
#include "project_identity.h"
#include "united_api.h"

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

static char    s_error[64];
static char    s_user[40];
static char    s_password[64];          /* kept for submitting results (this player's own only) */
static int32_t s_userId;
static char    s_apiKey[80];            /* (the new site's, for this session) */
static int32_t s_checking;              /* 1 the log-in is out, 2 the ban list */

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
const char *Account_Error(void)           { return s_error; }

int32_t     United_UserId(void)           { return s_userId; }
const char *United_UserName(void)         { return s_user; }
const char *United_Password(void)         { return s_password; }
const char *United_ApiKey(void)           { return s_apiKey; }

void Account_Begin(const char *user, const char *password)
{
    s_userId = 0;
    s_error[0] = 0;
    s_apiKey[0] = 0;
    snprintf(s_user, sizeof s_user, "%s", user);
    snprintf(s_password, sizeof s_password, "%s", password);
#if defined(SWOS_SDL_BUILD) && defined(SWOS_HTTPS)
    {
        /* The site matches names in capitals (SWOS 2020 sent _strupr'd ones); the password
         * as typed. In the body, never the URL: it stays out of the site's logs. */
        char up[40], u[128], p[200], form[400];
        uint32_t i;
        for (i = 0; user[i] && i < sizeof up - 1; i++)
            up[i] = (char)(user[i] >= 'a' && user[i] <= 'z' ? user[i] - 'a' + 'A' : user[i]);
        up[i] = 0;
        Https_Escape(up, u, sizeof u);
        Https_Escape(password, p, sizeof p);
        if (United_NewSite()) {
            snprintf(form, sizeof form, "username=%s&password=%s", u, p);
            s_checking = Https_RequestAsync(United_NewApiUrl("auth/key", NULL), form) == 0;
        } else {
            snprintf(form, sizeof form, "section=login&format=csv&usr=%s&pwd=%s", u, p);
            s_checking = Https_RequestAsync(United_ApiUrl("user", NULL), form) == 0;
        }
        memset(p, 0, sizeof p);
        memset(form, 0, sizeof form);
        if (!s_checking)
            snprintf(s_error, sizeof s_error, "COULD NOT START THE LOGIN");
    }
#else
    (void)password;
    snprintf(s_error, sizeof s_error, "THIS BUILD CANNOT LOG IN (NO HTTPS)");
#endif
}

#if defined(SWOS_SDL_BUILD) && defined(SWOS_HTTPS)
/* The site has said yes: now the ban list. 0 asked; else as after it (ban_verdict). */
static int32_t ban_verdict(void)
{
    if (!United_Banned(s_user, "ALL LOG IN"))
        return 1;
    fprintf(stderr, "[account] %s is banned from all log in\n", s_user);
    snprintf(s_error, sizeof s_error, "BANNED FROM ALL LOG IN. CONTACT THE SITE MANAGER.");
    s_userId = 0;
    s_apiKey[0] = 0;
    memset(s_password, 0, sizeof s_password);
    return -1;
}

static int32_t ask_bans(void)
{
    if (Https_RequestAsync(PROJECT_BAN_LIST, NULL) == 0) {
        s_checking = 2;
        return 0;
    }
    return ban_verdict();
}

/* The new site's answer (frees it): 1 logged in, -1 refused. */
static int32_t poll_new_site(HttpsReply *reply)
{
    UnitedJson all = { reply->body, (int32_t)reply->length };
    char status[32];
    const int32_t id = United_JsonInt(all, "$.user_id");
    United_JsonText(all, "$.status", status, sizeof status);
    if (strcmp(status, "ok") == 0 && id > 0) {
        s_userId = id;
        United_JsonText(all, "$.api_key", s_apiKey, sizeof s_apiKey);
        fprintf(stderr, "[account] logged in as %s (swos.swosunited.com)\n", s_user);
        Https_Free(reply);
        return ask_bans();
    }
    snprintf(s_error, sizeof s_error, "%s",
             strcmp(status, "too_many_attempts") == 0 ? "TOO MANY TRIES - WAIT 5 MINUTES" :
             strcmp(status, "missing_credentials") == 0 ? "TYPE YOUR USERNAME AND PASSWORD" :
                                                          "WRONG USERNAME OR PASSWORD");
    fprintf(stderr, "[account] login refused for %s (%s)\n", s_user, status[0] ? status : "?");
    Https_Free(reply);
    return -1;
}
#endif

int32_t Account_Poll(void)
{
#if defined(SWOS_SDL_BUILD) && defined(SWOS_HTTPS)
    HttpsReply reply;
    if (!s_checking)
        return s_userId > 0 ? 1 : -1;
    if (!Https_PollAsync(&reply))
        return 0;
    if (s_checking == 2) {
        s_checking = 0;
        s_banCount = 0;
        if (reply.status == 200 && reply.body)
            ban_lines(reply.body, '\n');
        else
            fprintf(stderr, "[account] no ban list (%d): nobody is banned this session\n", (int)reply.status);
        Https_Free(&reply);
        ban_lines(getenv("SWOS_UNITED_BANS_TEST"), ';');
        fprintf(stderr, "[account] ban list: %d lines\n", (int)s_banCount);
        return ban_verdict();
    }
    s_checking = 0;
    if (United_NewSite() && reply.body && (reply.status == 200 || reply.status == 400 ||
                                           reply.status == 401 || reply.status == 429))
        return poll_new_site(&reply);
    if (reply.status != 200 || !reply.body) {
        snprintf(s_error, sizeof s_error, "%s",
                 reply.status == HTTPS_ECERT ? "SENSIBLESOCCER.DE'S CERTIFICATE DID NOT CHECK OUT" :
                 reply.status == HTTPS_ENOCA ? "NO CERTIFICATE LIST (CACERT.PEM)" :
                 reply.status < 0            ? (United_NewSite() ? "CANNOT REACH SWOS.SWOSUNITED.COM" :
                                                                   "CANNOT REACH SENSIBLESOCCER.DE") :
                 United_NewSite()            ? "SWOS.SWOSUNITED.COM DID NOT ANSWER" :
                                               "SENSIBLESOCCER.DE DID NOT ANSWER");
        fprintf(stderr, "[account] login: no answer (%d)\n", (int)reply.status);
        Https_Free(&reply);
        return -1;
    }
    /* "<user id>;<stream key>": a number before the ';' is a good account. */
    {
        const char *semi = strchr(reply.body, ';');
        const int32_t id = semi ? atoi(reply.body) : 0;
        if (id > 0) {
            s_userId = id;
            fprintf(stderr, "[account] logged in as %s\n", s_user);
            Https_Free(&reply);
            return ask_bans();
        }
    }
    snprintf(s_error, sizeof s_error, "WRONG USERNAME OR PASSWORD");
    fprintf(stderr, "[account] login refused for %s\n", s_user);
    Https_Free(&reply);
    return -1;
#else
    return -1;
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
        fprintf(stderr, "[account] test login as %s: %s\n", s_user, r > 0 ? "good" : s_error);
        break;
    }
    fclose(f);
}
#else
static void test_login(void) {}
#endif

#endif /* PROJECT_ACCOUNTS */
