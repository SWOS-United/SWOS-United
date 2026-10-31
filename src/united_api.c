/*
 * united_api.c -- SWOS United's side of sensiblesoccer.de (include/united_api.h).
 */
#include "united_api.h"

#include <stdio.h>

#define UNITED_API_BASE "https://sensiblesoccer.de/index.php?option=com_swosapi"

const char *United_ApiUrl(const char *view, const char *query)
{
    static char url[512];
    snprintf(url, sizeof url, UNITED_API_BASE "&view=%s%s%s", view, query ? "&" : "", query ? query : "");
    return url;
}

#if defined(SWOS_SDL_BUILD) && defined(SWOS_HTTPS)
#include <stdlib.h>
#include "https_client.h"

/* SWOS_HTTPS_TEST=<url> [SWOS_HTTPS_TEST_FORM=<a=1&b=2>]: fetch it, print the status and the
 * start of the body, and exit -- the HTTPS client tried on its own, before the game starts
 * (run from bin/, where cacert.pem is). */
__attribute__((constructor)) static void united_https_test(void)
{
    const char *url = getenv("SWOS_HTTPS_TEST");
    HttpsReply r;
    if (!url || !url[0])
        return;
    Https_Request(url, getenv("SWOS_HTTPS_TEST_FORM"), &r);
    printf("[https-test] %s: status %d, %u bytes\n%.400s\n", url, (int)r.status, (unsigned)r.length,
           r.body ? r.body : "");
    Https_Free(&r);
    exit(r.status == 200 ? 0 : 1);
}
#endif
