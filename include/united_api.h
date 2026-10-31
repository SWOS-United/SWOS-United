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

/* The logged-in player (0 / "": nobody). */
int32_t     United_UserId(void);
const char *United_UserName(void);

#endif /* UNITED_API_H */
