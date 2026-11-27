/*
 * united_scn.h -- SWOS 2020's custom challenges (.SCN files) into OpenSWOS's challenges
 * (src/united_scn.c). SDL builds.
 */
#ifndef UNITED_SCN_H
#define UNITED_SCN_H

#include "swos_types.h"
#include "challenge_pack.h"     /* ChallengeDef */
#include "game_types.h"         /* TeamFileEx */

/* An .SCN (its bytes) into the player's fetched challenges: its teams as an .EDT and the
 * challenge in shared.json (the challenges mod's save folder), replacing one of the same name.
 * Its id into id[idCap] ("scn-<hash>"); the challenges service's "reload" then reads it. 1 done. */
int32_t UnitedScn_Import(const unsigned char *data, uint32_t size, char *id, uint32_t idCap);

/* One of OpenSWOS's challenges as SWOS 2020's .SCN, the reverse of UnitedScn_Import: its
 * challenge.ini (the section and SCENARIO NAME `name`: "<TEAM1> 1-2 <TEAM2> BY <USER>") and its two
 * teams' records as .TEAM files, in a 7z archive (stored). `teams` as in memory. *out is malloc'd
 * (free it). 1 done. */
int32_t UnitedScn_Export(const ChallengeDef *c, const TeamFileEx teams[2], const char *name, unsigned char **out,
                         uint32_t *outSize);

/* A 7z archive (its bytes): its first file whose name ends in `ext` (case ignored, ".rpl") into
 * *out (malloc'd: free it), *outSize its length. 1 found. (SWOS 2020's speedrun replays: a .7z
 * holding the run's .RPL.) */
int32_t United7z_FirstFile(const unsigned char *data, uint32_t size, const char *ext, unsigned char **out,
                           uint32_t *outSize);

#endif /* UNITED_SCN_H */
