/*
	fetch.c
	Copyright (c) 2021, Valentin Debon

	This file is part of the update program
	subject the BSD 3-Clause License, see LICENSE
*/
#include "fetch.h"

#include "set.h"

#include "schemes/file.h"
#include "schemes/https.h"

#include <stdlib.h>
#include <string.h>
#include <syslog.h>

struct scheme {
	const char *name;
	void (*open)(const struct state *state, const char *uri);
	void (*snapshot)(const struct state *state);
	void (*packages)(const struct state *state, const struct set *packages);
	void (*close)(const struct state *state);
};

static const struct scheme schemes[] = {
	{ /* File scheme, fetch directly from disk */
		FILE_SCHEME,
		file_scheme_open,
		file_scheme_snapshot,
		file_scheme_packages,
		file_scheme_close
	},
#if 0
	{ /* HTTPS scheme, secure fetch remotely */
		HTTPS_SCHEME,
		https_scheme_open,
		https_scheme_snapshot,
		https_scheme_packages,
		https_scheme_close
	},
#endif
};

static const struct scheme *scheme;

void
fetch_open(const struct state *state, const char *uri) {
	/* We first need to find the scheme class */
	const struct scheme *current = schemes,
		* const end = schemes + sizeof(schemes) / sizeof(*schemes);
	const size_t urilength = strlen(uri);

	while(current != end) {
		const char * const schemename = current->name;
		const char * const schemenameend = strchr(current->name, ':');

		if(schemenameend == NULL) {
			syslog(LOG_ERR, "Invalid scheme for uri '%s'", uri);
			exit(EXIT_FAILURE);
		}

		if(urilength == schemenameend - schemename
			&& strncasecmp(uri, schemename, urilength) == 0) {
			break;
		}
	}

	if(current == end) {
		syslog(LOG_ERR, "Unsupported scheme for uri '%s'", uri);
		exit(EXIT_FAILURE);
	}

	scheme = current;

	/* Then we can open it as is */
	scheme->open(state, uri);

	if(state->shouldexit) {
		exit(EXIT_SUCCESS);
	}
}

void
fetch_snapshot(const struct state *state) {
	scheme->snapshot(state);

	if(state->shouldexit) {
		exit(EXIT_SUCCESS);
	}
}

void
fetch_new_packages(const struct state *state, const struct set *newpackages) {
	scheme->packages(state, newpackages);

	if(state->shouldexit) {
		exit(EXIT_SUCCESS);
	}
}

void
fetch_close(const struct state *state) {
	scheme->close(state);

	if(state->shouldexit) {
		exit(EXIT_SUCCESS);
	}
}

