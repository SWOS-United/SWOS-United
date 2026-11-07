/*
 * united_ui.h -- SWOS United's online screens' own drawing and registration (src/united_ui.c,
 * src/united_*.c). The API they call is SWOS-API's client (third_party/swos-api/client,
 * include/united_api.h there).
 */
#ifndef UNITED_UI_H
#define UNITED_UI_H

#include "swos_types.h"

/* SWOS 2020's gameplay-style icons, 16x16 at (x, y) on the menu's screen: 0 the Amiga's tick,
 * 1 PC DOS, 2 XBOX (RANKINGS' buttons, CHOOSE GAMEPLAY's). SDL builds. */
void        United_DrawStyleIcon(int32_t icon, int32_t x, int32_t y);
/* SWOS 2020's red page arrow (drawScrollButtons), 12x12 at (x, y): up, or down. SDL builds. */
void        United_DrawPageArrow(int32_t down, int32_t x, int32_t y);
/* SWOS 2020's difficulty stars (its starsMap), n of them at (x, y) (src/united_challenges.c). */
void        United_DrawStars(int32_t n, int32_t x, int32_t y);
/* SWOS United's screens (registered by Account_Init). */
void        UnitedRankings_Register(void);
void        UnitedLeagues_Register(void);
void        UnitedMatches_Register(void);
#endif /* UNITED_UI_H */
