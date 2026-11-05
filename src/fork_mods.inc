/* fork_mods.inc -- SWOS United's own built-in mods (src/mods_builtin.c includes this; OpenSWOS
 * has no such file, so a merge of OpenSWOS never meets this list). One line a mod: its ModDef
 * (src/mod_<slug>.c), and its canonical manifest in bin/mods/<slug>/mod.json. */
extern const ModDef ModDef_menus2020;       /* SWOS 2020's main menu (docs/SWOS2020_MODS.md) */
extern const ModDef ModDef_unitedTicker;    /* SWOS 2020's news ticker, slug "ticker" */

#define FORK_MOD_LIST &ModDef_menus2020, &ModDef_unitedTicker,
