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
/* A rating's stars, in halves (1 = half a star .. 10 = five): SWOS Galore's */
void        United_DrawRating(int32_t halves, int32_t x, int32_t y);
/* An open leg of a tournament or a ranked match. */
typedef struct {
    uint32_t matchId, legId, tournaId;
    int32_t  rankingId;
    int32_t  group;                     /* 1 a group (league) match, 0 a knockout one, -1 not known yet */
    char     tournament[32], home[24], away[24];
} OpenLeg;

/* tournaInfoMenu to look at a player's open legs, not to pick one (src/united_matches.c). */
void        UnitedMatches_BrowseTournaments(const char *player, const OpenLeg *legs, int32_t count);

/* SWOS 2020's ban list (PROJECT_BAN_LIST, src/online_account_united.c): 1 when `name` is banned
 * from `mode` -- "ALL LOG IN", "FRIENDLY MATCHES", "RANKED MATCHES". */
int32_t     United_Banned(const char *name, const char *mode);

/* The star rating an SRN earns, 36x10 at (x, y) on the menu's screen (the lobby's, SWOS 2020's):
 * src/online_menus_fork.inc. */
void        OnlineMenus_DrawStars(int32_t srn, int32_t x, int32_t y);

/* SWOS United's screens (registered by Account_Init). */
void        UnitedRankings_Register(void);
void        UnitedLeagues_Register(void);
void        UnitedMatches_Register(void);
void        UnitedMatchInfo_Register(void);
#endif /* UNITED_UI_H */
