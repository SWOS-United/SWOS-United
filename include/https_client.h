/*
 * https_client.h -- HTTPS requests (SWOS United; native SDL builds with netplay, SWOS_HTTPS).
 *
 * For sensiblesoccer.de's API: the account log-in, the rankings, the leagues, the match
 * results (src/online_account_united.c, src/united_api.c). mbedTLS (third_party/mbedtls, built
 * by fork.mk) over a plain socket, the server's certificate checked against the CA list beside
 * the game (cacert.pem, Mozilla's, from curl.se) and the host name -- SWOS 2020 ran curl with
 * --insecure and checked neither.
 *
 * BLOCKING: a request takes as long as the network does. Call it from a thread of its own,
 * never the menu's (Https_RequestAsync does that).
 */
#ifndef HTTPS_CLIENT_H
#define HTTPS_CLIENT_H

#include "swos_types.h"

typedef struct {
    int32_t  status;            /* the HTTP status (200 ...), or < 0: did not get one (HTTPS_E*) */
    char    *body;              /* the reply's body, NUL-terminated (free with Https_Free); 0 none */
    uint32_t length;
} HttpsReply;

enum {
    HTTPS_ENET    = -1,         /* could not connect */
    HTTPS_ETLS    = -2,         /* the TLS handshake failed: no secure connection */
    HTTPS_ECERT   = -3,         /* the server's certificate did not check out */
    HTTPS_EPROTO  = -4,         /* not an HTTP reply we understand */
    HTTPS_ENOCA   = -5,         /* no CA list (cacert.pem) to check certificates with */
};

/* GET (form 0) or POST (form: "a=1&b=2", sent as application/x-www-form-urlencoded) `url`
 * ("https://host/path?query"); follows up to three redirects. Fills *reply; 0 if a reply came
 * (reply->status says what it was), else the HTTPS_E* in reply->status too. */
int32_t Https_Request(const char *url, const char *form, HttpsReply *reply);
void    Https_Free(HttpsReply *reply);

/* A form value, percent-encoded (RFC 3986's unreserved left as they are) into out[cap]. */
void    Https_Escape(const char *in, char *out, uint32_t cap);

/* The same request on a thread of its own: Begin, then Poll each frame until it is not 0 --
 * 1 done (*reply filled, the caller frees it), and the slot is free again. One at a time. */
int32_t Https_RequestAsync(const char *url, const char *form);    /* 0 started */
int32_t Https_PollAsync(HttpsReply *reply);

#endif /* HTTPS_CLIENT_H */
