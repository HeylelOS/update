#include "apply.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/wait.h>
#include <errno.h>
#include <err.h>

#include <hny.h>

static void
apply_new_geister_spawn(struct state *state, const char *geist, const char *path) {
	const char * const step = path + 4; /* path + 4 because strlen("hny/") == 4 */
	int wstatus;
	pid_t pid;

	/* If an error happens here, note no process was forked in hny_spawn. */
	const int errcode = hny_spawn(state->hny, geist, path, &pid);
	if(errcode != 0) {
		errx(EXIT_FAILURE, "apply_new_geister: Unable to spawn step %s for %s: %s", step, geist, strerror(errcode));
	}

	if(waitpid(pid, &wstatus, 0) != pid) {
		err(EXIT_FAILURE, "apply_new_geister: waitpid failed at step %s for %s", step, geist);
	}

	if(WIFSIGNALED(wstatus)) {
		errx(EXIT_FAILURE, "apply_new_geister: Spawned step %s for %s was ended with a signal: %s", step, geist, strsignal(WTERMSIG(wstatus)));
	}

	if(WIFEXITED(wstatus)) {
		const int status = WEXITSTATUS(wstatus);

		if(status != 0) {
			errx(EXIT_FAILURE, "apply_new_geister: Spawned step %s for %s exited with code %d", step, geist, status);
		}
	}
}

/*
 * To apply all geister we must check each case a new geist might represent:
 * - Previous geist installing a new package: Clean the old, shift the geist, setup the new.
 * - Previous geist installing an old package: Shift the geist, don't touch the package.
 * - New geist installing a new package: Shift the geist, setup the new.
 * - New geist installing an old package: Shift the geist, don't touch the package.
 */
void
apply_new_geister(struct state *state, const struct set *newgeister, const struct set *newpackages) {
	struct set_iterator newgeisteriterator;

	set_iterator_init(&newgeisteriterator, newgeister);

	const void *element;
	size_t elementsize;
	while(set_iterator_next(&newgeisteriterator, &element, &elementsize)) {
		const char * const geist = element;
		const char * const package = geist + strlen(geist) + 1;
		const bool isnewpackage = set_find(newpackages, package, NULL);
		int errcode;

		/* Cleaning the previous package if we're an old geist installing a new package */
		if(isnewpackage && set_find(&state->current, geist, NULL)) {
			apply_new_geister_spawn(state, geist, "hny/clean");
		}

		/* Shift it in any case */
		errcode = hny_shift(state->hny, geist, package);
		if(errcode != 0) {
			errx(EXIT_FAILURE, "apply_new_geister: Unable to shift %s to %s: %s", geist, package, strerror(errcode));
		}

		/* Setup the geist if we are a new package */
		if(isnewpackage) {
			apply_new_geister_spawn(state, geist, "hny/setup");
		}
	}

	set_iterator_deinit(&newgeisteriterator);
}

void
apply_pending(struct state *state) {
	if(unlinkat(state->dirfd, STATE_SNAPSHOT_CURRENT, 0) != 0) {
		err(EXIT_FAILURE, "apply_pending: Unable to remove " STATE_SNAPSHOT_CURRENT);
	}

	if(renameat(state->dirfd, STATE_SNAPSHOT_PENDING, state->dirfd, STATE_SNAPSHOT_CURRENT) != 0) {
		err(EXIT_FAILURE, "apply_pending: Unable to rename " STATE_SNAPSHOT_PENDING " to " STATE_SNAPSHOT_CURRENT);
	}

	set_empty(&state->pending);
	state_parse_current(state);
}

void
apply_cleanup(struct state *state) {
	const struct set * const packages = &state->packages;
	DIR *dirp = opendir(hny_path(state->hny));
	struct dirent *entry;

	if(dirp == NULL) {
		err(EXIT_FAILURE, "apply_cleanup: opendir %s", hny_path(state->hny));
	}

	while(errno = 0, entry = readdir(dirp), entry != NULL) {
		/* If hidden file/dir, or . or .., ignore it */
		if(*entry->d_name == '.') {
			continue;
		}

		switch(entry->d_type) {
		case DT_DIR:
			if(!set_find(packages, entry->d_name, NULL)) {
				const int errcode = hny_remove(state->hny, entry->d_name);

				if(errcode != 0) {
					errx(EXIT_FAILURE, "apply_cleanup: Unable to remove package %s: %s", entry->d_name, strerror(errcode));
				}
			}
			break;
		case DT_LNK:
			if(!set_find(&state->current, entry->d_name, NULL)) {
				/* There is a risk of removing a valid package if the geist pointed to a kept package. Just unlink. */
				const size_t geistlength = strlen(entry->d_name);
				const char * const prefixpath = hny_path(state->hny);
				const size_t prefixpathlength = strlen(prefixpath);
				char path[prefixpathlength + geistlength + 2]; /* One for the /, another for the terminating nul */

				strncpy(path, prefixpath, prefixpathlength);
				path[prefixpathlength] = '/';
				strncpy(path + prefixpathlength + 1, entry->d_name, geistlength + 1);

				if(unlink(path) != 0) {
					err(EXIT_FAILURE, "apply_cleanup: Unable to unlink %s", entry->d_name);
				}
			}
			break;
		default:
			/* Unknown sh*t, nothing to do here */
			warnx("Invalid entry in prefix %s: %s", hny_path(state->hny), entry->d_name);
			break;
		}
	}

	if(errno != 0) {
		err(EXIT_FAILURE, "apply_cleanup: readdir");
	}

	closedir(dirp);
}

