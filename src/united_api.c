/*
 * united_api.c -- SWOS United's side of sensiblesoccer.de (include/united_api.h).
 */
#include "united_api.h"
#include "project_identity.h"
#include "mjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UNITED_API_BASE "https://sensiblesoccer.de/index.php?option=com_swosapi"

static int32_t s_newSite;

void United_SetSite(const char *name)
{
    const char *env = getenv("SWOS_UNITED_SITE");
    if (env && env[0])
        name = env;
    s_newSite = name && strcmp(name, "swosunited") == 0;
}

int32_t United_NewSite(void) { return s_newSite; }

void United_NewApiUrlTo(char *url, uint32_t cap, const char *path, const char *query)
{
    snprintf(url, cap, PROJECT_ACCOUNT_API "%s%s%s", path, query ? "?" : "", query ? query : "");
}

const char *United_NewApiUrl(const char *path, const char *query)
{
    static char url[512];
    United_NewApiUrlTo(url, sizeof url, path, query);
    return url;
}

#ifdef SWOS_SDL_BUILD
#include "display.h"            /* FRONTBUFFER_ROW_STRIDE_BYTES */

/* The buttons' icons: SWOS 2020's mp_game_style_icon_set_v2_16x16_3x1_map (PixelmapAndStrings.h),
 * three 16x16 icons side by side -- the Amiga's tick, PC DOS, XBOX -- each a menu palette index
 * ('0'-'9', 'A'-'G' = 0-16), '.' left as it is, 32 in from the button's left and 2 down, as its
 * draw_mp_game_style_icon_sets. */
static const char *const k_styleIcons[16] = {
    "...........0000...00000.00000...................",
    "...........0AA0...011110011110.......22..22.....",
    "..........0AAA0...017710017770.....1111221111...",
    "..........0AAA0...011110010000.....2177117712...",
    ".........06660....017770011110....221177771122..",
    ".........06660....07000.077770...22221777712222.",
    "........06660...00000.000000000..22217711771222.",
    ".00000..0FFF0...0AAA000AD000FF00.22217122171222.",
    ".0BBB0.0FFF0....0ACCA0DBBA0FGGF0.22177122177122.",
    ".0BBB0.0FFF0....0AC0A0AB0D0FG0G0.21771222217712.",
    "..0BBB0EEE0.....0AC0A0DB0A0GFFF0.21712222221712.",
    "..0DDD0EEE0.....0AC0A0AB0D00GGF0..171222222171..",
    "...0DDEEE0......0AC0A0DB0A0F0GF0..212222222212..",
    "...0DDEEE0......0AAAC0BADB0GFFG0....22222222....",
    "....0EEE0.......0CCC000BB000GG00.....222222.....",
    "....00000.......00000.0000.0000.................",
};

/* SWOS 2020's drawScrollButtons arrows, red: up (cols 0-11) and down (12-23). */
static const char *const k_pageArrows[12] = {
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

void United_DrawPageArrow(int32_t down, int32_t ox, int32_t oy)
{
    extern volatile uintptr_t _frontbufferBasePtr;
    extern int _menu_draw_x_offset;
    uint8_t *fb = (uint8_t *)_frontbufferBasePtr + _menu_draw_x_offset;
    int32_t x, y;
    for (y = 0; y < 12; y++)
        for (x = 0; x < 12; x++) {
            const char c = k_pageArrows[y][(down ? 12 : 0) + x];
            if (c != '.')
                fb[(uint32_t)(oy + y) * FRONTBUFFER_ROW_STRIDE_BYTES + ox + x] =
                    (uint8_t)(c <= '9' ? c - '0' : c - 'A' + 10);
        }
}

void United_DrawStyleIcon(int32_t icon, int32_t ox, int32_t oy)
{
    extern volatile uintptr_t _frontbufferBasePtr;
    extern int _menu_draw_x_offset;
    uint8_t *fb = (uint8_t *)_frontbufferBasePtr + _menu_draw_x_offset;
    int32_t x, y;
    if (icon < 0 || icon > 2)
        return;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            const char c = k_styleIcons[y][icon * 16 + x];
            if (c != '.')
                fb[(uint32_t)(oy + y) * FRONTBUFFER_ROW_STRIDE_BYTES + ox + x] =
                    (uint8_t)(c <= '9' ? c - '0' : c - 'A' + 10);
        }
}
#endif

/* ---- JSON ---- */

static int32_t skip_ws(const char *p, int32_t n, int32_t i)
{
    while (i < n && (p[i] == ' ' || p[i] == '\t' || p[i] == '\r' || p[i] == '\n'))
        i++;
    return i;
}

/* Past the value that starts at i: a string, a nested array/object, or a scalar. */
static int32_t skip_value(const char *p, int32_t n, int32_t i)
{
    int32_t depth = 0;
    if (i >= n)
        return n;
    if (p[i] != '"' && p[i] != '[' && p[i] != '{') {
        while (i < n && p[i] != ',' && p[i] != ']' && p[i] != '}')
            i++;
        while (i > 0 && (p[i - 1] == ' ' || p[i - 1] == '\r' || p[i - 1] == '\n' || p[i - 1] == '\t'))
            i--;
        return i;
    }
    do {
        if (p[i] == '"') {
            for (i++; i < n && p[i] != '"'; i++)
                if (p[i] == '\\')
                    i++;
        } else if (p[i] == '[' || p[i] == '{') {
            depth++;
        } else if (p[i] == ']' || p[i] == '}') {
            depth--;
        }
        i++;
    } while (i < n && depth > 0);
    return i;
}

int32_t United_JsonFind(UnitedJson in, const char *path, UnitedJson *out)
{
    const char *tp;
    int tl;
    if (!in.p || in.n <= 0 || mjson_find(in.p, in.n, path, &tp, &tl) == MJSON_TOK_INVALID)
        return 0;
    out->p = tp;
    out->n = tl;
    return 1;
}

int32_t United_JsonNext(UnitedJson in, int32_t *pos, UnitedJson *key, UnitedJson *val)
{
    const char *p = in.p;
    const int32_t n = in.n;
    int32_t i = *pos, object;
    if (!p || n < 2 || (p[0] != '[' && p[0] != '{'))
        return 0;
    object = p[0] == '{';
    i = skip_ws(p, n, i == 0 ? 1 : i);
    if (i < n && p[i] == ',')
        i = skip_ws(p, n, i + 1);
    if (i >= n || p[i] == ']' || p[i] == '}')
        return 0;
    if (object) {
        int32_t k = skip_value(p, n, i);
        if (key) {
            key->p = p + i + 1;
            key->n = k - i - 2;
        }
        i = skip_ws(p, n, k);
        if (i >= n || p[i] != ':')
            return 0;
        i = skip_ws(p, n, i + 1);
    }
    val->p = p + i;
    i = skip_value(p, n, i);
    val->n = (int32_t)(p + i - val->p);
    *pos = i;
    return 1;
}

static int32_t hex4(const char *s)
{
    int32_t i, v = 0;
    for (i = 0; i < 4; i++) {
        const char c = s[i];
        v = v * 16 + (c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                      c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0);
    }
    return v;
}

/* A JSON string's text (tp/tl with its quotes) into out[cap], cut to fit. mjson_get_string gives
 * the whole string up for "\/" (PHP writes every '/' so), any \u past 00FF and a string longer
 * than the buffer, so it is decoded here: À-ÿ as the letter without its accent (the
 * menu font has plain letters only), anything else past ASCII as '?'. */
static void json_string(const char *tp, int32_t tl, char *out, uint32_t cap)
{
    static const char kLatin1[] = "AAAAAAACEEEEIIIIDNOOOOOxOUUUUYTsaaaaaaaceeeeiiiidnooooo/ouuuuyty";
    const char *s = tp + 1, *end = tp + tl - 1;
    uint32_t n = 0;
    while (s < end && n + 1 < cap) {
        int32_t c = (uint8_t)*s++;
        if (c == '\\' && s < end) {
            const char e = *s++;
            if (e == 'u' && end - s >= 4) {
                c = hex4(s);
                s += 4;
            } else {
                c = e == 'n' || e == 'r' || e == 't' || e == 'b' || e == 'f' ? ' ' : e;
            }
            if (c >= 0xC0 && c <= 0xFF)
                c = kLatin1[c - 0xC0];
            else if (c >= 0x80)
                c = '?';
        } else if (c >= 0x80) {                     /* (raw UTF-8: one '?' a character) */
            if (c >= 0xC0)
                c = '?';
            else
                continue;
        }
        out[n++] = (char)c;
    }
    out[n] = 0;
}

void United_JsonText(UnitedJson in, const char *path, char *out, uint32_t cap)
{
    const char *tp;
    int tl, tok;
    out[0] = 0;
    if (!in.p || in.n <= 0 || cap == 0)
        return;
    tok = mjson_find(in.p, in.n, path, &tp, &tl);
    if (tok == MJSON_TOK_STRING) {
        json_string(tp, tl, out, cap);
    } else if (tok == MJSON_TOK_NUMBER || tok == MJSON_TOK_TRUE || tok == MJSON_TOK_FALSE) {
        snprintf(out, cap, "%.*s", tl, tp);
    }
}

int32_t United_JsonInt(UnitedJson in, const char *path)
{
    char v[24];
    United_JsonText(in, path, v, sizeof v);
    return (int32_t)atol(v);
}

int32_t United_JsonOk(const char *body, uint32_t len)
{
    UnitedJson all = { body, (int32_t)len };
    char v[8];
    United_JsonText(all, "$.status", v, sizeof v);
    return strcmp(v, "ok") == 0;
}

void United_ApiUrlTo(char *url, uint32_t cap, const char *view, const char *query)
{
    snprintf(url, cap, UNITED_API_BASE "&view=%s%s%s", view, query ? "&" : "", query ? query : "");
}

const char *United_ApiUrl(const char *view, const char *query)
{
    static char url[512];
    United_ApiUrlTo(url, sizeof url, view, query);
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
