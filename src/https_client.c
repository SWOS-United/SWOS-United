/*
 * https_client.c -- HTTPS requests over mbedTLS (include/https_client.h). SWOS United.
 */
#include "https_client.h"

#if defined(SWOS_SDL_BUILD) && defined(SWOS_HTTPS)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "psa/crypto.h"
#include "swos_io.h"            /* swos_data_path: cacert.pem beside the game, or in its assets */

enum { MAX_REPLY = 4 << 20, MAX_REDIRECTS = 3, TIMEOUT_MS = 15000 };

static const char *const kUserAgent = "SWOSUnited/1";

/* "https://host[:port]/path" -> host, port, path. 0 ok. */
static int32_t split_url(const char *url, char *host, uint32_t hostCap, char *port, uint32_t portCap,
                         const char **path)
{
    const char *h, *end, *colon;
    uint32_t n;
    if (strncmp(url, "https://", 8) != 0)
        return -1;
    h = url + 8;
    end = strchr(h, '/');
    if (!end)
        end = h + strlen(h);
    colon = memchr(h, ':', (size_t)(end - h));
    n = (uint32_t)((colon ? colon : end) - h);
    if (n == 0 || n >= hostCap)
        return -1;
    memcpy(host, h, n);
    host[n] = 0;
    if (colon) {
        n = (uint32_t)(end - colon - 1);
        if (n == 0 || n >= portCap)
            return -1;
        memcpy(port, colon + 1, n);
        port[n] = 0;
    } else {
        snprintf(port, portCap, "443");
    }
    *path = *end ? end : "/";
    return 0;
}

/* The CA list: loaded once. */
static mbedtls_x509_crt s_ca;
static int32_t s_caLoaded;              /* 1 loaded, -1 could not be */
static SDL_mutex *s_caLock;

static int32_t ca_list(void)
{
    if (!s_caLock)
        s_caLock = SDL_CreateMutex();
    SDL_LockMutex(s_caLock);
    if (!s_caLoaded) {
        char path[1024];
        mbedtls_x509_crt_init(&s_ca);
        swos_data_path("cacert.pem", path, sizeof path);
        s_caLoaded = mbedtls_x509_crt_parse_file(&s_ca, path) >= 0 ? 1 : -1;
        if (s_caLoaded < 0)                 /* (beside the game: the working directory, bin/) */
            s_caLoaded = mbedtls_x509_crt_parse_file(&s_ca, "cacert.pem") >= 0 ? 1 : -1;
        if (s_caLoaded < 0)
            fprintf(stderr, "[https] no CA list at %s: certificates cannot be checked\n", path);
    }
    SDL_UnlockMutex(s_caLock);
    return s_caLoaded > 0;
}

/* Reads until the server closes (Connection: close) or MAX_REPLY. */
static int32_t read_all(mbedtls_ssl_context *ssl, char **out, uint32_t *len)
{
    uint32_t cap = 16384, n = 0;
    char *buf = (char *)malloc(cap + 1);
    if (!buf)
        return -1;
    for (;;) {
        int r;
        if (n == cap) {
            char *b;
            if (cap >= MAX_REPLY)
                break;
            cap *= 2;
            b = (char *)realloc(buf, cap + 1);
            if (!b) {
                free(buf);
                return -1;
            }
            buf = b;
        }
        r = mbedtls_ssl_read(ssl, (unsigned char *)buf + n, cap - n);
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE ||
            r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET)
            continue;
        if (r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || r == 0)
            break;
        if (r < 0) {
            if (n > 0)
                break;              /* (a server that just closes the socket: what came is the reply) */
            free(buf);
            return -1;
        }
        n += (uint32_t)r;
    }
    buf[n] = 0;
    *out = buf;
    *len = n;
    return 0;
}

static int32_t header(const char *head, const char *name, char *out, uint32_t cap)
{
    const uint32_t nl = (uint32_t)strlen(name);
    const char *p = head;
    while ((p = strstr(p, "\r\n")) != NULL) {
        p += 2;
        if (SDL_strncasecmp(p, name, nl) == 0 && p[nl] == ':') {
            const char *v = p + nl + 1, *e;
            uint32_t n;
            while (*v == ' ')
                v++;
            e = strstr(v, "\r\n");
            n = (uint32_t)(e ? e - v : (long)strlen(v));
            if (n >= cap)
                n = cap - 1;
            memcpy(out, v, n);
            out[n] = 0;
            return 1;
        }
    }
    return 0;
}

/* A chunked body, in place. */
static uint32_t dechunk(char *body, uint32_t len)
{
    char *in = body, *out = body, *end = body + len;
    for (;;) {
        char *e;
        unsigned long size = strtoul(in, &e, 16);
        char *data = strstr(in, "\r\n");
        if (!data || e == in)
            break;
        data += 2;
        if (size == 0 || data + size > end)
            break;
        memmove(out, data, size);
        out += size;
        in = data + size + 2;
        if (in >= end)
            break;
    }
    *out = 0;
    return (uint32_t)(out - body);
}

static int32_t one_request(const char *url, const char *form, HttpsReply *reply, char *location,
                           uint32_t locationCap)
{
    char host[256], port[8], *req = NULL, *raw = NULL;
    const char *path;
    mbedtls_net_context net;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    uint32_t rawLen = 0, reqLen;
    int32_t ret = HTTPS_EPROTO;
    int r;

    location[0] = 0;
    if (split_url(url, host, sizeof host, port, sizeof port, &path) != 0)
        return HTTPS_EPROTO;
    if (!ca_list())
        return HTTPS_ENOCA;

    mbedtls_net_init(&net);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);

    if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)kUserAgent,
                              strlen(kUserAgent)) != 0) {
        ret = HTTPS_ETLS;
        goto done;
    }
    if (mbedtls_net_connect(&net, host, port, MBEDTLS_NET_PROTO_TCP) != 0) {
        ret = HTTPS_ENET;
        goto done;
    }
    if (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT) != 0) {
        ret = HTTPS_ETLS;
        goto done;
    }
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &s_ca, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
    mbedtls_ssl_conf_read_timeout(&conf, TIMEOUT_MS);
    if (mbedtls_ssl_setup(&ssl, &conf) != 0 || mbedtls_ssl_set_hostname(&ssl, host) != 0) {
        ret = HTTPS_ETLS;
        goto done;
    }
    mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);
    while ((r = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
            ret = mbedtls_ssl_get_verify_result(&ssl) != 0 ? HTTPS_ECERT : HTTPS_ETLS;
            if (ret == HTTPS_ECERT)
                fprintf(stderr, "[https] %s: the certificate did not check out (%08x)\n", host,
                        (unsigned)mbedtls_ssl_get_verify_result(&ssl));
            goto done;
        }
    }

    reqLen = (uint32_t)(strlen(path) + strlen(host) + (form ? strlen(form) : 0) + 512);
    req = (char *)malloc(reqLen);
    if (!req)
        goto done;
    if (form)
        snprintf(req, reqLen, "POST %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nConnection: close\r\n"
                 "Content-Type: %s\r\nContent-Length: %u\r\n\r\n%s",
                 path, host, kUserAgent, form[0] == '{' ? "application/json" : "application/x-www-form-urlencoded",
                 (unsigned)strlen(form), form);
    else
        snprintf(req, reqLen, "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: %s\r\nConnection: close\r\n\r\n",
                 path, host, kUserAgent);
    {
        const unsigned char *p = (const unsigned char *)req;
        size_t left = strlen(req);
        while (left > 0) {
            r = mbedtls_ssl_write(&ssl, p, left);
            if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE)
                continue;
            if (r <= 0) {
                ret = HTTPS_ENET;
                goto done;
            }
            p += r;
            left -= (size_t)r;
        }
    }
    if (read_all(&ssl, &raw, &rawLen) != 0) {
        ret = HTTPS_ENET;
        goto done;
    }
    {
        char *bodyStart = strstr(raw, "\r\n\r\n"), value[512];
        int status = 0;
        if (!bodyStart || sscanf(raw, "HTTP/%*s %d", &status) != 1)
            goto done;
        bodyStart[2] = 0;                       /* (the head ends at its last CRLF) */
        bodyStart += 4;
        reply->status = status;
        reply->length = rawLen - (uint32_t)(bodyStart - raw);
        reply->body = (char *)malloc(reply->length + 1);
        if (!reply->body)
            goto done;
        memcpy(reply->body, bodyStart, reply->length);
        reply->body[reply->length] = 0;
        if (header(raw, "Transfer-Encoding", value, sizeof value) && SDL_strncasecmp(value, "chunked", 7) == 0)
            reply->length = dechunk(reply->body, reply->length);
        if (status >= 300 && status < 400 && header(raw, "Location", value, sizeof value))
            snprintf(location, locationCap, "%s", value);
        ret = 0;
    }
done:
    if (ret == 0)
        mbedtls_ssl_close_notify(&ssl);
    free(req);
    free(raw);
    mbedtls_net_free(&net);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    return ret;
}

int32_t Https_Request(const char *url, const char *form, HttpsReply *reply)
{
    static int32_t psaReady;
    char current[1024], location[1024];
    int32_t hop, r = HTTPS_EPROTO;

    memset(reply, 0, sizeof *reply);
    if (!psaReady) {                    /* (TLS 1.3 in mbedTLS 3.x needs the PSA layer) */
        if (psa_crypto_init() != PSA_SUCCESS) {
            reply->status = HTTPS_ETLS;
            return HTTPS_ETLS;
        }
        psaReady = 1;
    }
    snprintf(current, sizeof current, "%s", url);
    for (hop = 0; hop <= MAX_REDIRECTS; hop++) {
        Https_Free(reply);
        r = one_request(current, form, reply, location, sizeof location);
        if (r != 0) {
            reply->status = r;
            return r;
        }
        if (!location[0])
            return 0;
        if (strncmp(location, "https://", 8) == 0) {
            snprintf(current, sizeof current, "%s", location);
        } else if (location[0] == '/') {        /* the same host */
            char host[256], port[8];
            const char *path;
            if (split_url(current, host, sizeof host, port, sizeof port, &path) != 0)
                return 0;
            snprintf(current, sizeof current, "https://%s%s%s%s", host, strcmp(port, "443") ? ":" : "",
                     strcmp(port, "443") ? port : "", location);
        } else {
            return 0;                           /* (not https: not followed) */
        }
        form = NULL;                            /* (a redirect is fetched with GET) */
    }
    return r;
}

void Https_Free(HttpsReply *reply)
{
    free(reply->body);
    reply->body = NULL;
    reply->length = 0;
}

void Https_Escape(const char *in, char *out, uint32_t cap)
{
    static const char hex[] = "0123456789ABCDEF";
    uint32_t n = 0;
    for (; *in && n + 4 < cap; in++) {
        const unsigned char c = (unsigned char)*in;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out[n++] = (char)c;
        } else {
            out[n++] = '%';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 15];
        }
    }
    out[n] = 0;
}

/* ---- one request at a time on a thread of its own ---- */

static struct {
    SDL_Thread  *thread;
    SDL_atomic_t done;
    char        *url, *form;
    HttpsReply   reply;
} s_async;

static int async_main(void *unused)
{
    (void)unused;
    Https_Request(s_async.url, s_async.form, &s_async.reply);
    SDL_AtomicSet(&s_async.done, 1);
    return 0;
}

int32_t Https_RequestAsync(const char *url, const char *form)
{
    if (s_async.thread)
        return 1;
    s_async.url  = SDL_strdup(url);
    s_async.form = form ? SDL_strdup(form) : NULL;
    SDL_AtomicSet(&s_async.done, 0);
    s_async.thread = SDL_CreateThread(async_main, "https", NULL);
    if (!s_async.thread) {
        SDL_free(s_async.url);
        SDL_free(s_async.form);
        return 1;
    }
    return 0;
}

int32_t Https_PollAsync(HttpsReply *reply)
{
    if (!s_async.thread || !SDL_AtomicGet(&s_async.done))
        return 0;
    SDL_WaitThread(s_async.thread, NULL);
    s_async.thread = NULL;
    *reply = s_async.reply;
    memset(&s_async.reply, 0, sizeof s_async.reply);
    SDL_free(s_async.url);
    SDL_free(s_async.form);
    s_async.url = s_async.form = NULL;
    return 1;
}

#endif /* SWOS_SDL_BUILD && SWOS_HTTPS */
