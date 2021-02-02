/*
	schemes/file.h
	Copyright (c) 2021, Valentin Debon

	This file is part of the update program
	subject the BSD 3-Clause License, see LICENSE
*/
#ifndef UPDATE_SCHEMES_FILE_H
#define UPDATE_SCHEMES_FILE_H

#include "../set.h"
#include "../state.h"

#define FILE_SCHEME "file"

void
file_scheme_open(const struct state *state, const char *uri);

void
file_scheme_snapshot(const struct state *state);

void
file_scheme_packages(const struct state *state, const struct set *packages);

void
file_scheme_close(const struct state *state);

/* UPDATE_SCHEMES_FILE_H */
#endif
