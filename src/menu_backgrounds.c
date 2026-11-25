/*
 * menu_backgrounds.c -- SWOS United's own menu backgrounds, without a mod (2026-11-25):
 * APPEARANCE OPTIONS' MENU BACKGROUND, [graphics] menu_background = amiga (the default: black and
 * purple) | pc (blue and black). Two 7680 x 2160 pictures, bin/data/backgrounds/amiga.png and
 * pc.png, drawn behind every menu by the presenter as the backgrounds mod's are -- but by their
 * own rule (the backdrop's rule 1, platform_sdl.c / gpu_present.c): at the picture's 3X they are
 * shown pixel for pixel from their centre, more of them as the window is wider or bigger, scaled
 * up only where they no longer fill it, and below 3X scaled down with the picture, so they sit as
 * at 3X. With the backgrounds mod on, its picture is used, as before (MenuBackdrop_Pick).
 *
 * The chosen one is decoded on a thread of its own (a picture this size takes a while), from the
 * start and again when another is chosen; the first backdrop asked for waits for it, the
 * window's events handled meanwhile. SDL builds.
 */
#ifdef SWOS_SDL_BUILD
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "swos_io.h"
#include "../third_party/stb_image/stb_image.h"

static const char *const k_files[2] = { "data/backgrounds/amiga.png", "data/backgrounds/pc.png" };

static struct {
    int           which;        /* the one asked for (MENU_BACKGROUND_*), -1 none yet */
    SDL_Thread   *thread;
    SDL_atomic_t  done;
    int           decoding;     /* (which the thread decodes) */
    uint8_t      *rgba, *next;  /* the one in use; the thread's, until taken */
    int           w, h, nw, nh;
    int           have;         /* rgba is `which`'s */
} s_mb = { -1 };

static int SDLCALL decode_thread(void *unused)
{
    char path[1100];
    int n = 0;
    const Uint32 t0 = SDL_GetTicks();
    (void)unused;
    s_mb.next = stbi_load(swos_data_path(k_files[s_mb.decoding], path, (uint32_t)sizeof path),
                          &s_mb.nw, &s_mb.nh, &n, 4);
    fprintf(stderr, "[menu-background] %s: %s (%d x %d, %u ms on a thread)\n", k_files[s_mb.decoding],
            s_mb.next ? "decoded" : "could not be read", s_mb.nw, s_mb.nh, (unsigned)(SDL_GetTicks() - t0));
    SDL_AtomicSet(&s_mb.done, 1);
    return 0;
}

/* The thread's picture taken (waiting for it, the window's events handled): 1 when there is one. */
static void take(void)
{
    if (!s_mb.thread)
        return;
    while (!SDL_AtomicGet(&s_mb.done)) {
        SDL_PumpEvents();
        SDL_Delay(2);
    }
    SDL_WaitThread(s_mb.thread, NULL);
    s_mb.thread = NULL;
    if (s_mb.decoding == s_mb.which) {
        stbi_image_free(s_mb.rgba);
        s_mb.rgba = s_mb.next;
        s_mb.w = s_mb.nw;
        s_mb.h = s_mb.nh;
        s_mb.have = s_mb.rgba != NULL;
    } else {
        stbi_image_free(s_mb.next);     /* (another one chosen meanwhile) */
    }
    s_mb.next = NULL;
}

/* MENU BACKGROUND `which` (0 amiga, 1 pc): decoded on its own thread unless it is the one there. */
void MenuBackgrounds_Choose(int which)
{
    which = which == 1 ? 1 : 0;
    if (s_mb.which == which && (s_mb.have || s_mb.thread))
        return;
    take();                             /* (one thread at a time) */
    s_mb.which = which;
    if (s_mb.have && s_mb.decoding == which)
        return;
    s_mb.have = 0;
    s_mb.decoding = which;
    SDL_AtomicSet(&s_mb.done, 0);
    s_mb.thread = SDL_CreateThread(decode_thread, "menubg", NULL);
    if (!s_mb.thread)
        decode_thread(NULL);            /* (no thread: here, now) */
}

/* The menus' backdrop to show and the rule it is drawn by (0: covering the window, a mod's
 * picture; 1: SWOS United's own, by their 3X rule). 0 when there is none: the game's own. */
int MenuBackdrop_Pick(const uint8_t **rgba, int32_t *w, int32_t *h, int *rule)
{
    extern int32_t ModHooks_MenuBackdrop(const uint8_t **rgba, int32_t *w, int32_t *h);
    if (ModHooks_MenuBackdrop(rgba, w, h) && *rgba) {
        *rule = 0;
        return 1;
    }
    if (s_mb.which < 0)
        return 0;
    take();
    if (!s_mb.have)
        return 0;
    *rgba = s_mb.rgba;
    *w = s_mb.w;
    *h = s_mb.h;
    *rule = 1;
    return 1;
}
#endif /* SWOS_SDL_BUILD */
