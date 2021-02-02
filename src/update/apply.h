/*
	apply.h
	Copyright (c) 2021, Valentin Debon

	This file is part of the update program
	subject the BSD 3-Clause License, see LICENSE
*/
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
