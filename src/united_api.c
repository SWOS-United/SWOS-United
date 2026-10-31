/*
 * united_api.c -- SWOS United's side of sensiblesoccer.de (include/united_api.h).
 */
#include "united_api.h"

#include <stdio.h>
#include <string.h>

#define UNITED_API_BASE "https://sensiblesoccer.de/index.php?option=com_swosapi"

const char *United_ApiUrl(const char *view, const char *query)
{
    static char url[512];
    snprintf(url, sizeof url, UNITED_API_BASE "&view=%s%s%s", view, query ? "&" : "", query ? query : "");
    return url;
}

const char *United_XmlBlock(const char *p, const char *end, const char *tag, const char **blockEnd)
{
    char open[64], close[64];
    const char *a, *b;
    snprintf(open, sizeof open, "<%s>", tag);
    snprintf(close, sizeof close, "</%s>", tag);
    a = p ? strstr(p, open) : NULL;
    if (!a || a >= end)
        return NULL;
    b = strstr(a, close);
    if (!b || b >= end)
        return NULL;
    *blockEnd = b + strlen(close);
    return a + strlen(open);
}

const char *United_XmlText(const char *p, const char *end, const char *tag, char *out, uint32_t cap)
{
    const char *after, *a = United_XmlBlock(p, end, tag, &after), *b;
    uint32_t n = 0;
    out[0] = 0;
    if (!a)
        return NULL;
    b = after - strlen(tag) - 3;            /* (the start of </tag>) */
    while (a < b && n + 1 < cap) {
        static const struct { const char *ent; char c; } k[5] = {
            { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' }, { "&apos;", '\'' },
        };
        int32_t i, hit = 0;
        if (*a == '&')
            for (i = 0; i < 5; i++)
                if (strncmp(a, k[i].ent, strlen(k[i].ent)) == 0) {
                    out[n++] = k[i].c;
                    a += strlen(k[i].ent);
                    hit = 1;
                    break;
                }
        if (!hit)
            out[n++] = *a++;
    }
    out[n] = 0;
    return after;
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
