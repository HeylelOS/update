#include "annul.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <err.h>

#include <hny.h>

void
annul_pending(struct state *state) {
	/* We only have to remove the file, and empty the pending set */

	if(unlinkat(state->dirfd, STATE_SNAPSHOT_PENDING, 0) != 0) {
		err(EXIT_FAILURE, "annul_pending: Unable to remove " STATE_SNAPSHOT_PENDING " snapshot");
	}

	set_empty(&state->pending);
}

static void
annul_new_geister_spawn(struct state *state, const char *geist, const char *path) {
	const char * const step = path + 4; /* path + 4 because strlen("hny/") == 4 */
	int wstatus;
	pid_t pid;

	/* If an error happens here, note no process was forked in hny_spawn. */
	const int errcode = hny_spawn(state->hny, geist, path, &pid);
	if(errcode != 0) {
		errx(EXIT_FAILURE, "annul_new_geister: Unable to spawn step %s for %s: %s", step, geist, strerror(errcode));
	}

	if(waitpid(pid, &wstatus, 0) != pid) {
		err(EXIT_FAILURE, "annul_new_geister: waitpid failed at step %s for %s", step, geist);
	}

	if(WIFSIGNALED(wstatus)) {
		errx(EXIT_FAILURE, "annul_new_geister: Spawned step %s for %s was ended with a signal: %s", step, geist, strsignal(WTERMSIG(wstatus)));
	}

	if(WIFEXITED(wstatus)) {
		const int status = WEXITSTATUS(wstatus);

		if(status != 0) {
			errx(EXIT_FAILURE, "annul_new_geister: Spawned step %s for %s exited with code %d", step, geist, status);
		}
	}
}

/*
 * To annul all geister we must check each case a new geist might represent:
 * - Previous geist installing a new package: Deinstall the package, shift back the geist.
 * - Previous geist installing an old package: Shift back the geist, don't touch the package.
 * - New geist installing a new package: Deinstall the package, unlink the geist.
 * - New geist installing an old package: Unlink the geist, don't touch the package.
 */
void
annul_new_geister(struct state *state, const struct set *newgeister, const struct set *newpackages) {
	struct set_iterator newgeisteriterator;

	set_iterator_init(&newgeisteriterator, newgeister);

	const void *element;
	size_t elementsize;
	while(set_iterator_next(&newgeisteriterator, &element, &elementsize)) {
		const size_t geistlength = strlen(element);
		const char * const geist = element;
		const char * const package = geist + geistlength + 1;
		const bool isnewpackage = set_find(newpackages, package, NULL);
		int errcode;

		/* Clean new package */
		if(isnewpackage) {
			annul_new_geister_spawn(state, geist, "hny/clean");
		}

		if(set_find(&state->current, geist, &element)) {
			/* If it was a previous geist, shift it back, and setup the old package */
			const char * const oldpackage = (const char *)element + geistlength + 1;

			errcode = hny_shift(state->hny, geist, oldpackage);
			if(errcode != 0) {
				errx(EXIT_FAILURE, "annul_new_geister: Unable to shift %s to %s: %s", geist, oldpackage, strerror(errcode));
			}

			if(isnewpackage) {
				annul_new_geister_spawn(state, geist, "hny/setup");
			}
		} else {
			/* If it was a new geist, we will only unlink the geist, not remove its content. */
			const char * const prefixpath = hny_path(state->hny);
			const size_t prefixpathlength = strlen(prefixpath);
			char path[prefixpathlength + geistlength + 2]; /* One for the /, another for the terminating nul */

			strncpy(path, prefixpath, prefixpathlength);
			path[prefixpathlength] = '/';
			strncpy(path + prefixpathlength + 1, geist, geistlength + 1);

			if(unlink(path) != 0) {
				err(EXIT_FAILURE, "annul_new_geister: Unable to unlink %s", geist);
			}
		}
	}

	set_iterator_deinit(&newgeisteriterator);
}

