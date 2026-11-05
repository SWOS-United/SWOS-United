/*
 * mod_united_ticker.c -- SWOS 2020's news ticker (render.cpp showNewsStr) in SWOS United: the
 * `ticker` mod, bin/mods/ticker, a fork mod (src/fork_mods.inc). SDL builds.
 *
 * Until 2026-11-05 OpenSWOS's (src/mod_ticker.c there, 0d924c1d); SWOS 2020's look and services
 * are United's, so it came here and OpenSWOS kept the pieces any mod uses: a menu's
 * "ticker_entry" (docs/MENUS.md §13, menus2020's main menu has one), ModApi_MenuTickerEntry,
 * ModApi_DrawText / TextWidth, ModApi_RestoreMenuBackground.
 *
 * A line of news scrolling right to left through that box, one item a pass, the next picked at
 * random, as SWOS 2020's. The news is JSON, "openswos-news":
 *
 *   { "format": "openswos-news", "version": 1,
 *     "items": [ { "text": "...", "link": "https://..." }, ... ] }
 *
 * read from the player's save/mods/ticker/news.json first, else the mod's own
 * data_json/ticker/news.json. The feed: SWOS 2020's ticker read tips.txt, which its launcher
 * downloaded from sensiblesoccer.de (a text line, then its link, a pair per item); here the
 * game fetches it itself, once a run, in the background (Https_RequestAsync), and keeps it as
 * that JSON in the save folder -- never the .txt (docs/SWOS2020_MODS.md: "json is our format for
 * data"). Setting "speed": slow / normal / fast.
 */
#include "mod_api.h"

#ifdef SWOS_SDL_BUILD

#include "mjson.h"
#include "https_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SLUG "ticker"

enum { kMaxItems = 64, kTextLen = 160 };

static char    s_text[kMaxItems][kTextLen];
static int32_t s_count, s_loaded, s_item = -1;
static int32_t s_offset;                /* the text's left edge, from the box's right edge */
static uint32_t s_lastTick, s_seed = 0x2545F491u;

static uint32_t next_random(void)       /* (its own: the game's numbers are the match's) */
{
    s_seed ^= s_seed << 13;
    s_seed ^= s_seed >> 17;
    s_seed ^= s_seed << 5;
    return s_seed;
}

static void parse(const char *json, int len)
{
    char fmt[32] = "";
    mjson_get_string(json, len, "$.format", fmt, (int)sizeof fmt);
    if (strcmp(fmt, "openswos-news") != 0)
        return;
    for (int32_t i = 0; s_count < kMaxItems; i++) {
        char q[48];
        snprintf(q, sizeof q, "$.items[%d].text", (int)i);
        if (mjson_get_string(json, len, q, s_text[s_count], kTextLen) < 0)
            break;
        for (char *c = s_text[s_count]; *c; c++)    /* (the menu fonts are capitals) */
            if (*c >= 'a' && *c <= 'z')
                *c = (char)(*c - 32);
        if (s_text[s_count][0])
            s_count++;
    }
}

/* ---- the feed ---- */
#define FEED_URL "https://sensiblesoccer.de/swos2020/tips.txt"

static int32_t s_feed;                  /* 0 not asked, 1 asking, 2 done (or given up) */

static void json_string(char *out, size_t cap, size_t *len, const char *in, size_t n)
{
    for (size_t i = 0; i < n && *len + 8 < cap; i++) {
        const unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') {
            out[(*len)++] = '\\';
            out[(*len)++] = (char)c;
        } else if (c >= 0x20 && c < 0x7F) {
            out[(*len)++] = (char)c;
        }                               /* (control and non-ASCII bytes: the menu font has none) */
    }
}

/* tips.txt -> openswos-news JSON in the save folder; 1 when it held any item. */
static int32_t keep_feed(const char *body, uint32_t length)
{
    static char json[65536];
    size_t len = 0;
    int32_t items = 0;
    const char *p = body, *end = body + length;
    len += (size_t)snprintf(json, sizeof json,
                            "{\n \"format\": \"openswos-news\",\n \"version\": 1,\n"
                            " \"source\": \"%s\",\n \"items\": [", FEED_URL);
    while (p < end && items < kMaxItems) {
        const char *text = p, *textEnd, *link, *linkEnd;
        while (p < end && *p != '\n') p++;
        textEnd = p;
        if (p < end) p++;
        link = p;
        while (p < end && *p != '\n') p++;
        linkEnd = p;
        if (p < end) p++;
        while (textEnd > text && (textEnd[-1] == '\r' || textEnd[-1] == ' ')) textEnd--;
        while (linkEnd > link && (linkEnd[-1] == '\r' || linkEnd[-1] == ' ')) linkEnd--;
        if (textEnd == text)
            continue;
        if (len + 64 + (size_t)(textEnd - text) + (size_t)(linkEnd - link) * 2 >= sizeof json)
            break;
        len += (size_t)snprintf(json + len, sizeof json - len, "%s\n  { \"text\": \"",
                                items ? "," : "");
        json_string(json, sizeof json, &len, text, (size_t)(textEnd - text));
        len += (size_t)snprintf(json + len, sizeof json - len, "\", \"link\": \"");
        json_string(json, sizeof json, &len, link, (size_t)(linkEnd - link));
        len += (size_t)snprintf(json + len, sizeof json - len, "\" }");
        items++;
    }
    len += (size_t)snprintf(json + len, sizeof json - len, "\n ]\n}\n");
    if (!items || len >= sizeof json)
        return 0;
    return ModApi_SaveFile(SLUG, "news.json", json, (uint32_t)len) == 0;
}

/* Each menu frame until it is done: ask once (the one request slot may be busy -- a league
 * screen's: ask again next frame), then take the answer. */
static void feed_step(void)
{
#ifndef SWOS_HTTPS
    s_feed = 2;                         /* (no HTTPS client in this build -- the web's: the
                                         * news kept before, else the mod's own) */
#else
    HttpsReply reply;
    if (s_feed == 0) {
        if (Https_RequestAsync(FEED_URL, NULL) == 0)
            s_feed = 1;
        return;
    }
    if (s_feed != 1 || !Https_PollAsync(&reply))
        return;
    s_feed = 2;
    if (reply.status == 200 && reply.body && keep_feed(reply.body, reply.length)) {
        s_loaded = 0;                   /* read again, the news just kept */
        s_count = 0;
        s_item = -1;
        fprintf(stderr, "[ticker] the news from %s kept\n", FEED_URL);
    } else {
        fprintf(stderr, "[ticker] %s: no news (status %d), the ones kept before stay\n",
                FEED_URL, (int)reply.status);
    }
    Https_Free(&reply);
#endif
}

static void load(void)
{
    uint32_t size = 0;
    char *json;
    s_loaded = 1;
    json = (char *)ModApi_LoadFile(SLUG, "news.json", &size);
    if (json) {
        parse(json, (int)size);
        free(json);
    }
    if (!s_count) {
        char path[1200];
        const char *dir = ModApi_ModDir(SLUG);
        FILE *f;
        snprintf(path, sizeof path, "%s/data_json/ticker/news.json", dir ? dir : ".");
        f = fopen(path, "rb");
        if (f) {
            static char buf[16384];
            const size_t n = fread(buf, 1, sizeof buf - 1, f);
            fclose(f);
            buf[n] = 0;
            parse(buf, (int)n);
        }
    }
    s_seed ^= ModApi_Ticks();
    fprintf(stderr, "[ticker] %d news items\n", (int)s_count);
}

/* Each menu frame: the box painted back, the line drawn where it has scrolled to, clipped to the
 * box a character at a time (the menu text has no clipping of its own). */
static void ticker_menu_drawn(void)
{
    const int32_t entry = ModApi_MenuTickerEntry();
    int32_t bx, by, bw, bh;
    if (entry < 0)
        return;
    if (s_feed != 2)
        feed_step();
    if (!s_loaded)
        load();
    if (!s_count)
        return;
    ModApi_MenuEntryRect(entry, &bx, &by, &bw, &bh);
    if (bw <= 0 || bh <= 0)
        return;

    const char *speed = ModApi_Setting(SLUG, "speed", "normal");
    const int32_t pxPerSec = !strcmp(speed, "slow") ? 30 : !strcmp(speed, "fast") ? 90 : 50;
    const uint32_t now = ModApi_Ticks();
    if (s_item < 0 || !s_lastTick) {
        s_item = (int32_t)(next_random() % (uint32_t)s_count);
        s_offset = 0;
        s_lastTick = now;
    }
    s_offset += (int32_t)((now - s_lastTick) * (uint32_t)pxPerSec / 1000u);
    if ((now - s_lastTick) * (uint32_t)pxPerSec >= 1000u)
        s_lastTick = now;

    const char *text = s_text[s_item];
    const int32_t width = ModApi_TextWidth(text, 1);
    if (s_offset > bw + width) {            /* gone off the left: the next one */
        s_item = (int32_t)(next_random() % (uint32_t)s_count);
        s_offset = 0;
    }

    ModApi_RestoreMenuBackground(bx, by, bw, bh);
    const int32_t y = by + (bh - 6) / 2;
    int32_t x = bx + bw - s_offset;
    for (const char *c = text; *c; c++) {
        char one[2] = { *c, 0 };
        const int32_t cw = ModApi_TextWidth(one, 1);
        if (x >= bx && x + cw <= bx + bw && *c != ' ')
            ModApi_DrawText(x, y, 2, one, 1);
        x += cw;
        if (x >= bx + bw)
            break;
    }
}

const ModDef ModDef_unitedTicker = {
    .slug = SLUG,
    .menuDrawn = ticker_menu_drawn,
};

#endif /* SWOS_SDL_BUILD */
