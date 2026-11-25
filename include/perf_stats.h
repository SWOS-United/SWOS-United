/*
 * perf_stats.h -- what a step costs, measured where it runs (SDL builds; 2026-11-25).
 *
 * With SWOS_PERF=1 in the environment each named step is timed between Perf_Begin and Perf_End,
 * and every 250 of it SWOS.log gets "[perf] <step> avg <ms> max <ms>". Without it the two calls
 * are a test of one flag. For comparing a change before and after (~/swos_united_perf_plan.md):
 *
 *   PERF_GAME      a match frame (GameLoop_Frame: the simulation and the drawing of it)
 *   PERF_NETPLAY   an online match's simulated frame, the rollbacks' again included (each
 *                  GameLoop_Frame the netplay step runs)
 *   PERF_MENU      a menu's drawing (DrawMenu, its pictures and the mods' with it)
 *   PERF_PRESENT   the presenter's own CPU work for a frame, up to the GPU's or SDL's present
 *                  (not the wait for the screen)
 */
#ifndef PERF_STATS_H
#define PERF_STATS_H

#include <stdint.h>

enum { PERF_GAME, PERF_NETPLAY, PERF_MENU, PERF_PRESENT, PERF_STEPS };

#ifdef SWOS_SDL_BUILD
extern int g_perfOn;
void Perf_Init(void);
void Perf_Begin(int step);
void Perf_End(int step);
#else
#define Perf_Init()      ((void)0)
#define Perf_Begin(s)    ((void)0)
#define Perf_End(s)      ((void)0)
#endif

#endif
