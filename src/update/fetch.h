#ifndef UPDATE_FETCH_H
#define UPDATE_FETCH_H

#include "set.h"
#include "state.h"

void
fetch_open(const struct state *state, const char *uri);

void
fetch_snapshot(const struct state *state);

void
fetch_new_packages(const struct state *state, const struct set *newpackages);

void
fetch_close(const struct state *state);

/* UPDATE_FETCH_H */
#endif
