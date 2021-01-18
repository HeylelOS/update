#ifndef UPDATE_ANNUL_H
#define UPDATE_ANNUL_H

#include "state.h"
#include "set.h"

void
annul_pending(struct state *state);

void
annul_new_geister(struct state *state, const struct set *newgeister, const struct set *newpackages);

/* UPDATE_ANNUL_H */
#endif
