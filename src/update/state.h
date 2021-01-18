#ifndef UPDATE_STATE_H
#define UPDATE_STATE_H

#include <time.h>

#include <hny.h>

#include "set.h"

#define STATE_SNAPSHOT_CURRENT "current"
#define STATE_SNAPSHOT_PENDING "pending"

struct state {
	struct hny *hny;   /* Honey prefix of system */
	int dirfd;         /* File descriptor for directory of snapshot and pending */

	struct set current; /* Current state geister */
	struct set pending; /* Pending state geister */

	struct set packages; /* Packages of current */
};

void
state_init(struct state *state, const char *prefix, int flags, const char *snapshots);

void
state_deinit(struct state *state);

void
state_diff(const struct state *state, struct set *newgeister, struct set *newpackages);

void
state_parse_pending(struct state *state);

void
state_parse_current(struct state *state);

/* UPDATE_STATE_H */
#endif
