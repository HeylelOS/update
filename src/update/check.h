/*
	check.h
	Copyright (c) 2021, Valentin Debon

	This file is part of the update program
	subject the BSD 3-Clause License, see LICENSE
*/
#ifndef UPDATE_CHECK_H
#define UPDATE_CHECK_H

#include <stdbool.h>

#include "set.h"
#include "state.h"

bool
check_pending(struct state *state);

bool
check_new_geister(struct state *state, const struct set *newgeister);

/* UPDATE_CHECK_H */
#endif
