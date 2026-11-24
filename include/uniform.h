/* uniform.h -- the kit a side wears for ONE match, chosen on the UNIFORM screen
 * (src/uniform_menu.c, docs/UNIFORM.md). SDL only.
 *
 * A choice never touches the team: it is kept beside it until kick-off, where the
 * match dresses the side from it (Uniform_Kits). SWOS's own rule for kits that are too
 * alike still runs, on what was chosen. */
#ifndef UNIFORM_H
#define UNIFORM_H

#include "game_types.h"
#include "kit_art.h"

/* A new match is being set up: nobody has chosen anything yet. */
void Uniform_Clear(void);

/* The two team headers a match takes its kits from: copies of the teams', with a side's
 * choice written over BOTH its kits -- so whichever of the two SWOS's rule picks for
 * that side is the one chosen. */
int32_t Uniform_Kits(const TeamFile *team1, const TeamFile *team2, TeamFile *kit1, TeamFile *kit2);   /* 1: a side chose */

/* This team's kit for the match, as if chosen on the screen. */
void Uniform_Choose(const TeamFile *team, const KitLook *look);

/* What was chosen for this team, if anything; forgotten once taken. For the online
 * lobby, which sends its side's choice to the other player inside the team. */
int32_t Uniform_Take(const TeamFile *team, KitLook *out);

/* Whether a kit is chosen for this team; `kits` (may be NULL): the team header the match
 * would take its kits from, the choice over both kits (as Uniform_Kits). */
int32_t Uniform_Chosen(const TeamFile *team, TeamFile *kits);

/* The UNIFORM screen for the two teams of the PLAY MATCH screen: `left` is the team
 * that screen is showing, `leftIsTop` says whether it is the match's first team (the
 * one SWOS's kit rule favours). */
void UniformMenu_Init(void);
void UniformMenu_Open(TeamFileEx *left, TeamFileEx *right, int32_t leftIsTop);

/* The online lobby says so around its team screen, where the PLAY MATCH screen has no
 * way to know that the other side is someone else's: it is shown and cannot be changed,
 * or is not shown at all while the other player has no team (otherKnown 0). -1 = not
 * online. `localIsTop`: this player is the match's first team; `otherChosen`: the other
 * player chose his kit; `kitRule`: the rule the match's kits are chosen by (KIT_RULE_*). */
void UniformMenu_SetOnline(int32_t otherKnown, int32_t localIsTop, int32_t otherChosen, int32_t kitRule);

#endif /* UNIFORM_H */
