/*
 * online_account_united.c -- SWOS United's accounts (include/online_account.h): a player logs
 * in to their sensiblesoccer.de account, as in SWOS 2020 (its PHPManager::logIn, now in
 * third_party/swos-api): a POST of the username and password to the site's API, over HTTPS
 * with its certificate checked (src/https_client.c). The reply is "<user id>;<stream key>" for a
 * good account, anything else for a bad one.
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
static int32_t s_checking;

static void test_login(void);

void Account_Init(void)
{
    UnitedRankings_Register();
    UnitedMatches_Register();
    test_login();
}

int32_t Account_Has(AccountFeature feature)
{
    return feature == ACCOUNT_HAS_RANKINGS || feature == ACCOUNT_HAS_RANKED;   /* (LEAGUES, MATCH LOGS: to come) */
}

int32_t     Account_Enabled(void)         { return 1; }
const char *Account_Prompt(void)          { return "LOGIN TO YOUR SENSIBLESOCCER.DE ACCOUNT"; }
const char *Account_Url(AccountLink link) { return (uint32_t)link < ACCOUNT_LINKS ? kUrls[link] : 0; }
const char *Account_Error(void)           { return s_error; }

int32_t     United_UserId(void)           { return s_userId; }
const char *United_UserName(void)         { return s_user; }
const char *United_Password(void)         { return s_password; }

void Account_Begin(const char *user, const char *password)
{
    s_userId = 0;
    s_error[0] = 0;
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
        snprintf(form, sizeof form, "section=login&format=csv&usr=%s&pwd=%s", u, p);
        s_checking = Https_RequestAsync(United_ApiUrl("user", NULL), form) == 0;
        if (!s_checking)
            snprintf(s_error, sizeof s_error, "COULD NOT START THE LOGIN");
    }
#else
    (void)password;
    snprintf(s_error, sizeof s_error, "THIS BUILD CANNOT LOG IN (NO HTTPS)");
#endif
}

int32_t Account_Poll(void)
{
#if defined(SWOS_SDL_BUILD) && defined(SWOS_HTTPS)
    HttpsReply reply;
    if (!s_checking)
        return s_userId > 0 ? 1 : -1;
    if (!Https_PollAsync(&reply))
        return 0;
    s_checking = 0;
    if (reply.status != 200 || !reply.body) {
        snprintf(s_error, sizeof s_error, "%s",
                 reply.status == HTTPS_ECERT ? "SENSIBLESOCCER.DE'S CERTIFICATE DID NOT CHECK OUT" :
                 reply.status == HTTPS_ENOCA ? "NO CERTIFICATE LIST (CACERT.PEM)" :
                 reply.status < 0            ? "CANNOT REACH SENSIBLESOCCER.DE" :
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
            return 1;
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
