/*
	check.c
	Copyright (c) 2021, Valentin Debon

	This file is part of the update program
	subject the BSD 3-Clause License, see LICENSE
*/
#include "check.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>

bool
check_pending(struct state *state) {
	/* If we do not have a pending snapshot, everything's fine */
	return set_is_empty(&state->pending);
}

bool
check_new_geister(struct state *state, const struct set *newgeister) {
	struct set_iterator newgeisteriterator;

	set_iterator_init(&newgeisteriterator, newgeister);

	bool foundone = false;
	const void *element;
	size_t elementsize;
	while(!state->shouldexit && set_iterator_next(&newgeisteriterator, &element, &elementsize)) {
		/* As we only need to check if one of the new geist is correct,
		 * we won't bother handling every cases of new geist like in annul or apply,
		 * here, we'll only check if the geist is present with the right target */
		const char * const geist = element;
		const size_t geistlength = strlen(geist);
		const char * const package = geist + geistlength + 1;
		const size_t packagelength = strlen(package);
		const char * const prefixpath = hny_path(state->hny);
		const size_t prefixpathlength = strlen(prefixpath);
		char path[prefixpathlength + geistlength + 2]; /* One for the /, another for the terminating nul */
		char dest[packagelength + 1];

		strncpy(path, prefixpath, prefixpathlength);
		path[prefixpathlength] = '/';
		strncpy(path + prefixpathlength + 1, geist, geistlength + 1);

		ssize_t destlength = readlink(path, dest, sizeof(dest));

		if(destlength == -1) {
			if(errno != ENOENT) {
				syslog(LOG_ERR, "check_new_geister: Unable to readlink %s: %m", path);
				exit(EXIT_FAILURE);
			}
		} else {
			dest[packagelength] = '\0';
			if(destlength == packagelength && strcmp(package, dest) == 0) {
				foundone = true;
				break;
			}
		}
	}

	set_iterator_deinit(&newgeisteriterator);

	if(state->shouldexit) {
		exit(EXIT_SUCCESS);
	}

	return foundone;
}

