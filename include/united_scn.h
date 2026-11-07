/*
 * united_scn.h -- SWOS 2020's custom challenges (.SCN files) into OpenSWOS's challenges
 * (src/united_scn.c). SDL builds.
 */
#ifndef UNITED_SCN_H
#define UNITED_SCN_H

#include "swos_types.h"

/* An .SCN (its bytes) into the player's fetched challenges: its teams as an .EDT and the
 * challenge in shared.json (the challenges mod's save folder), replacing one of the same name.
 * Its id into id[idCap] ("scn-<hash>"); the challenges service's "reload" then reads it. 1 done. */
int32_t UnitedScn_Import(const unsigned char *data, uint32_t size, char *id, uint32_t idCap);

#endif /* UNITED_SCN_H */
