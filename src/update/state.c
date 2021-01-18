#include "state.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <err.h>

void
state_init(struct state *state, const char *prefix, int flags, const char *snapshots) {
	int errcode = hny_open(&state->hny, prefix, flags);
	if(errcode != 0) {
		errx(EXIT_FAILURE, "update_init: Unable to open prefix at %s: %s", prefix, strerror(errcode));
	}

	errcode = hny_lock(state->hny);
	if(errcode != 0) {
		errx(EXIT_FAILURE, "update_init: Unable to lock prefix %s: %s", prefix, strerror(errcode));
	}

	state->dirfd = open(snapshots, O_RDONLY | O_DIRECTORY);
	if(state->dirfd < 0) {
		err(EXIT_FAILURE, "update_init: Unable to open snapshots at %s", prefix);
	}

	/* Four states are accepted in the following section:
	 * 1- The current snapshot is present, not pending:
	 *    We should have a clean state, consistency should not encounter anything. Parse current.
	 * 2- The current snapshot is present, so is pending: 
	 *    If pending is empty, the pending snapshot creation was interrupted, erase the file, and go to step 1.
	 *    Else, pending was interrupted before commit, both snapshots are parsed, the consistency
	 *    step will cleanup all uncommited new files and geister for us.
	 * 3- The current snapshot is not present, pending is:
	 *    We were interrupted on commiting pending, rename pending and go to step 1.
	 * 4- The current snapshot is not present, neither is pending:
	 *    We are making a blank system install, just initialize both sets to empty, the fetch step will fill pending.
	 */

	set_init(&state->current, &pair_set_class);
	set_init(&state->pending, &pair_set_class);

	set_init(&state->packages, &string_set_class);

	const bool hascurrent = faccessat(state->dirfd, STATE_SNAPSHOT_CURRENT, F_OK, AT_SYMLINK_NOFOLLOW) == 0;
	const bool haspending = faccessat(state->dirfd, STATE_SNAPSHOT_PENDING, F_OK, AT_SYMLINK_NOFOLLOW) == 0;

	if(hascurrent) {

		state_parse_current(state);

		if(haspending) {
			struct stat st;

			if(fstatat(state->dirfd, STATE_SNAPSHOT_PENDING, &st, AT_SYMLINK_NOFOLLOW) != 0) {
				err(EXIT_FAILURE, "state_init: Unable to stat " STATE_SNAPSHOT_PENDING " snapshot");
			}

			if(st.st_size > 0) {
				state_parse_pending(state);
			} else {
				if(unlinkat(state->dirfd, STATE_SNAPSHOT_PENDING, 0) != 0) {
					err(EXIT_FAILURE, "state_init: Unable to unlink " STATE_SNAPSHOT_PENDING " snapshot");
				}
			}
		}
	} else {
		if(haspending) {

			if(renameat(state->dirfd, STATE_SNAPSHOT_PENDING, state->dirfd, STATE_SNAPSHOT_CURRENT) != 0) {
				err(EXIT_FAILURE, "state_init: Unable to rename " STATE_SNAPSHOT_PENDING " snapshot to " STATE_SNAPSHOT_CURRENT);
			}

			state_parse_current(state);
		}
	}
}

void
state_deinit(struct state *state) {
	hny_unlock(state->hny);

	hny_close(state->hny);
	close(state->dirfd);

	set_deinit(&state->current);
	set_deinit(&state->pending);

	set_deinit(&state->packages);
}

void
state_diff(const struct state *state, struct set *newgeister, struct set *newpackages) {
	struct set_iterator pendingiterator;

	set_iterator_init(&pendingiterator, &state->pending);

	const void *element;
	size_t elementsize;
	while(set_iterator_next(&pendingiterator, &element, &elementsize)) {
		const char * const geist = element;
		const size_t geistlength = strlen(geist);
		const char * const package = geist + geistlength + 1;

		set_insert(newgeister, geist);
		if(!set_find(&state->packages, package, NULL)) {
			set_insert(newpackages, package);
		}
	}

	set_iterator_deinit(&pendingiterator);
}

#define SWAP(T, a, b) do {\
	T const c = a;\
	a = b;\
	b = c;\
} while(0)

static void
parse_snapshot(struct set *snapshot, const char *filename) {
	FILE *filep = fopen(filename, "r");

	if(filep == NULL) {
		err(EXIT_FAILURE, "parse_snapshot: Unable to open %s", filename);
	}

	enum {
		PARSE_SNAPSHOT_BEGIN,
		PARSE_SNAPSHOT_NEXT_GEIST,
		PARSE_SNAPSHOT_EXPECT_PACKAGE,
	} parsing = PARSE_SNAPSHOT_BEGIN;
	char *geist = NULL, *line = NULL;
	size_t geistn = 0, linen = 0, lineno = 1;
	ssize_t geistlength, linelength;

	while(errno = 0, linelength = getline(&line, &linen, filep), linelength != -1) {
		const char *nul = memchr(line, '\0', linelength);

		if(nul != NULL) {
			errx(EXIT_FAILURE, "parse_snapshot: Ill formed snapshot %s contains zero byte at line %lu", filename, lineno);
		}

		if(line[linelength] == '\n') {
			line[linelength] = '\0';
			linelength--;
		}

		enum hny_type type = hny_type_of(line);

		switch(parsing) {
		case PARSE_SNAPSHOT_NEXT_GEIST:
			if(type == HNY_TYPE_PACKAGE) {
				break;
			}
		case PARSE_SNAPSHOT_BEGIN:
			if(type == HNY_TYPE_GEIST) {
				SWAP(char *, geist, line);
				SWAP(size_t, geistn, linen);
				SWAP(ssize_t, geistlength, linelength);

				if(set_find(snapshot, geist, NULL)) {
					errx(EXIT_FAILURE, "parse_snapshot: Ill formed snapshot %s redundant geist %s at line %lu", filename, geist, lineno);
				}

				parsing = PARSE_SNAPSHOT_EXPECT_PACKAGE;
				break;
			} else {
				errx(EXIT_FAILURE, "parse_snapshot: Ill formed snapshot %s does not have a geist at line %lu", filename, lineno);
			}
		case PARSE_SNAPSHOT_EXPECT_PACKAGE:
			if(type == HNY_TYPE_PACKAGE) {
				char pair[geistlength + linelength + 2]; /* 2 for two terminating nul bytes */
				strncpy(pair, geist, geistlength);
				pair[geistlength] = '\0';
				strncpy(pair + geistlength + 1, line, linelength);
				pair[sizeof(pair) - 1] = '\0';

				set_insert(snapshot, pair);

				parsing = PARSE_SNAPSHOT_NEXT_GEIST;
				break;
			} else {
				errx(EXIT_FAILURE, "parse_snapshot: Ill formed snapshot %s does not have a geist as first entry", filename);
			}
		}

		lineno++;
	}

	if(errno != 0) {
		err(EXIT_FAILURE, "parse_snapshot: Unable to read line from %s", filename);
	}

	free(geist);
	free(line);

	fclose(filep);
}

void
state_parse_pending(struct state *state) {
	set_empty(&state->pending);
	parse_snapshot(&state->pending, STATE_SNAPSHOT_PENDING);
}

void
state_parse_current(struct state *state) {
	set_empty(&state->current);
	parse_snapshot(&state->current, STATE_SNAPSHOT_CURRENT);

	/* Refresh packages set state */
	set_empty(&state->packages);
	struct set_iterator currentiterator;

	set_iterator_init(&currentiterator, &state->current);

	const void *element;
	size_t elementsize;
	while(set_iterator_next(&currentiterator, &element, &elementsize)) {
		const char * const package = (const char *)element + strlen(element) + 1;

		set_insert(&state->packages, package);
	}

	set_iterator_deinit(&currentiterator);
}

