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

void Account_Init(void)
{
    UnitedRankings_Register();
    UnitedLeagues_Register();
    UnitedMatches_Register();
    test_login();
}

int32_t Account_Has(AccountFeature feature)
{
    return feature == ACCOUNT_HAS_RANKINGS || feature == ACCOUNT_HAS_RANKED ||
           feature == ACCOUNT_HAS_LEAGUES;      /* (MATCH LOGS: to come) */
}

int32_t     Account_Enabled(void)         { return 1; }
const char *Account_Prompt(void)
{
    return United_NewSite() ? "LOGIN TO YOUR SWOS UNITED ACCOUNT" : "LOGIN TO YOUR SENSIBLESOCCER.DE ACCOUNT";
}
const char *Account_Url(AccountLink link) { return (uint32_t)link < ACCOUNT_LINKS ? kUrls[link] : 0; }
const char *Account_Error(void)           { return United_LoginError(); }
void        Account_Begin(const char *user, const char *password) { United_LoginBegin(user, password); }
int32_t     Account_Poll(void)            { return United_LoginPoll(); }

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
