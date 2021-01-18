#ifndef UPDATE_APPLY_H
#define UPDATE_APPLY_H

#include "state.h"
#include "set.h"

void
apply_new_geister(struct state *state, const struct set *newgeister, const struct set *newpackages);

void
apply_pending(struct state *state);

void
apply_cleanup(struct state *state);

/* UPDATE_APPLY_H */
#endif
