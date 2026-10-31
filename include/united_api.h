/*
 * united_api.h -- SWOS United's side of sensiblesoccer.de (src/united_api.c): its API's
 * addresses, and who is logged in (src/online_account_united.c).
 */
#ifndef UNITED_API_H
#define UNITED_API_H

#include "swos_types.h"

/* The site's API: "https://sensiblesoccer.de/index.php?option=com_swosapi&view=<view>", and
 * "&<query>" after it when given. In a static buffer: copy it before the next call. */
const char *United_ApiUrl(const char *view, const char *query);

/* The API's replies are flat XML. The text of the first <tag>...</tag> in [p, end) into out
 * (entities &amp; &lt; &gt; &quot; &apos; decoded); the text just past it, or NULL with out
 * "" when there is none. */
const char *United_XmlText(const char *p, const char *end, const char *tag, char *out, uint32_t cap);
/* The next <tag>...</tag> block in [p, end): *blockEnd set past it; NULL when none. */
const char *United_XmlBlock(const char *p, const char *end, const char *tag, const char **blockEnd);

/* SWOS United's screens (registered by Account_Init). */
void        UnitedRankings_Register(void);

/* The logged-in player (0 / "": nobody). */
int32_t     United_UserId(void);
const char *United_UserName(void);

#endif /* UNITED_API_H */
