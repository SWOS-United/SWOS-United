/*
 * perf_stats.c -- include/perf_stats.h: each step's time, with SWOS_PERF=1, into SWOS.log.
 */
#ifdef SWOS_SDL_BUILD

#include "perf_stats.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

int g_perfOn;

static const char *const k_names[PERF_STEPS] = { "game frame", "netplay frame", "menu draw", "present" };
static struct { Uint64 t0, sum, max; int n; } s_step[PERF_STEPS];
static double s_toMs;

void Perf_Init(void)
{
    g_perfOn = getenv("SWOS_PERF") != NULL;
    s_toMs = 1000.0 / (double)SDL_GetPerformanceFrequency();
    if (g_perfOn)
        fprintf(stderr, "[perf] on: each step's average and longest, every 250 of it\n");
}

void Perf_Begin(int step)
{
    if (g_perfOn && step >= 0 && step < PERF_STEPS)
        s_step[step].t0 = SDL_GetPerformanceCounter();
}

void Perf_End(int step)
{
    if (!g_perfOn || step < 0 || step >= PERF_STEPS || !s_step[step].t0)
        return;
    const Uint64 d = SDL_GetPerformanceCounter() - s_step[step].t0;
    s_step[step].t0 = 0;
    s_step[step].sum += d;
    if (d > s_step[step].max)
        s_step[step].max = d;
    if (++s_step[step].n >= 250) {
        fprintf(stderr, "[perf] %-13s avg %7.3f ms  max %7.3f ms  (%d)\n", k_names[step],
                (double)s_step[step].sum * s_toMs / s_step[step].n, (double)s_step[step].max * s_toMs,
                s_step[step].n);
        s_step[step].sum = s_step[step].max = 0;
        s_step[step].n = 0;
    }
}

#endif /* SWOS_SDL_BUILD */
