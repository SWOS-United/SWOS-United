/*
 * united_ticker.h -- the `ticker` mod's news (src/mod_united_ticker.c), for whoever lists it too
 * (menus2020's TIPS AND REVIEWS / STREAMS AND VIDEOS). SDL builds.
 */
#ifndef UNITED_TICKER_H
#define UNITED_TICKER_H

#include <stdint.h>

/* Each news item, in order, to `add`: its text and its link ("" none) -- the player's
 * save/mods/ticker/news.json, else (none there, or no item in it) the mod's own
 * data_json/ticker/news.json. An item that cannot be read (not an object, its text missing, too
 * long, not ASCII) is skipped, never the end of the list. Returns the items given. */
int32_t UnitedTicker_News(void (*add)(const char *text, const char *link, void *ctx), void *ctx);

#endif /* UNITED_TICKER_H */
