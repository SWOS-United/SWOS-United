/* fork_mods.inc -- SWOS United's own built-in mods (src/mods_builtin.c includes this; OpenSWOS
 * has no such file, so a merge of OpenSWOS never meets this list). One line a mod: its ModDef
 * (src/mod_<slug>.c), and its canonical manifest in bin/mods/<slug>/mod.json. */
extern const ModDef ModDef_menus2020;       /* SWOS 2020's main menu (docs/SWOS2020_MODS.md) */
extern const ModDef ModDef_unitedTicker;    /* SWOS 2020's news ticker, slug "ticker" */
extern const ModDef ModDef_onlineRecords;   /* the challenges' and speedruns' records online (SWOS 2020's) */
extern const ModDef ModDef_challenges2020;  /* SWOS 2020's challenge screens (src/united_challenges.c) */
extern const ModDef ModDef_speedrun2020;    /* SWOS 2020's speedrun screens (src/united_speedrun.c) */
extern const ModDef ModDef_galore2020;      /* SWOS 2020's SWOS GALORE, its charts (src/united_galore.c) */
extern const ModDef ModDef_display2020;     /* SWOS 2020's scoreboard and result band (src/mod_display2020.c) */

/* (MOD_SDL: both are the SDL build's -- the mods list is built on the Amiga too since OpenSWOS
 *  ab481ba9; mods_builtin.c defines MOD_SDL / MOD_AMIGA before the list expands this) */
#define FORK_MOD_LIST MOD_SDL(ModDef_menus2020) MOD_SDL(ModDef_unitedTicker) MOD_SDL(ModDef_onlineRecords) \
                      MOD_SDL(ModDef_challenges2020) MOD_SDL(ModDef_speedrun2020) \
                      MOD_SDL(ModDef_galore2020) MOD_SDL(ModDef_display2020)
