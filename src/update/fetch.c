#include "fetch.h"

#include "set.h"

#include "schemes/file.h"

#include <stdlib.h>
#include <string.h>
#include <err.h>

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
			errx(EXIT_FAILURE, "Invalid scheme for uri '%s'", uri);
		}

		if(urilength == schemenameend - schemename
			&& strncasecmp(uri, schemename, urilength) == 0) {
			break;
		}
	}

	if(current == end) {
		errx(EXIT_FAILURE, "Unsupported scheme for uri '%s'", uri);
	}

	scheme = current;

	/* Then we can open it as is */
	scheme->open(state, uri);
}

void
fetch_snapshot(const struct state *state) {
	scheme->snapshot(state);
}

void
fetch_new_packages(const struct state *state, const struct set *newpackages) {
	scheme->packages(state, newpackages);
}

void
fetch_close(const struct state *state) {
	scheme->close(state);
}

