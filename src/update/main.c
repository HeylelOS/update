#include "check.h"
#include "fetch.h"
#include "apply.h"
#include "annul.h"
#include "state.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdnoreturn.h>
#include <err.h>

struct update_args {
	char *prefix;
	char *snapshots;
	int flags;
};

static struct state state;

static void
update_consistency(struct state *state) {
	/* If pending snapshot hasn't been committed */
	if(!check_pending(state)) {
		struct set newgeister, newpackages;

		set_init(&newgeister, &pair_set_class);
		set_init(&newpackages, &string_set_class);
		state_diff(state, &newgeister, &newpackages);

		/* Check if at least one of the new geister was installed, if not,
		 * we cannot guarantee all packages where fetched,
		 * and we should remove them all. */
		const bool allnewpackagesfetched = check_new_geister(state, &newgeister);
		annul_new_geister(state, &newgeister, &newpackages);

		if(allnewpackagesfetched) {
			apply_new_geister(state, &newgeister, &newpackages);
			apply_pending(state);
		} else {
			/* Uncommitted packages will be removed during cleanup */
			annul_pending(state);
		}

		set_deinit(&newgeister);
		set_deinit(&newpackages);
	}

	/* Remove all deprecated packages/geister,
	 * whether they're old ones, or uncommitted new. */
	apply_cleanup(state);
}

static void
update_perform(struct state *state, const char *uri) {
	struct set newgeister, newpackages;

	/******************
	 * Fetch sequence *
	 ******************/

	/* Open uri, could be a socket, file... */
	fetch_open(state, uri);

	/* Snapshot is fetched, put on disk as pending, and parsed */
	fetch_snapshot(state);

	/* Now that we have a pending snapshot, compute the difference between the updates */
	set_init(&newgeister, &pair_set_class);
	set_init(&newpackages, &string_set_class);
	state_diff(state, &newgeister, &newpackages);

	/* Newer packages are downloaded and installed at the same time */
	fetch_new_packages(state, &newpackages);

	/* Close uri */
	fetch_close(state);

	/**************************
	 * "True" update sequence *
	 **************************/

	/* New geister are shifted, deprecated geister/packages are cleaned */
	apply_new_geister(state, &newgeister, &newpackages);

	set_deinit(&newgeister);
	set_deinit(&newpackages);

	/* The pending snapshot is commited */
	apply_pending(state);

	/* The prefix is cleaned up if dirty */
	apply_cleanup(state);
}

static void noreturn
update_usage(const char *updatename, int status) {
	fprintf(stderr, "usage: %s [-hb] [-p <prefix>] [-s <snapshots>] <uri>\n", updatename);
	exit(status);
}

static struct update_args
update_parse_args(int argc, char **argv) {
	struct update_args args = {
		.prefix = getenv("HNY_PREFIX"),
		.snapshots = "/data/update",
		.flags = 0,
	};
	int c;

	while((c = getopt(argc, argv, ":hbp:s:H:")) != -1) {
		switch(c) {
		case 'h':
			update_usage(*argv, EXIT_SUCCESS);
		case 'b':
			args.flags |= HNY_FLAGS_BLOCK;
			break;
		case 'p':
			args.prefix = optarg;
			break;
		case 's':
			args.snapshots = optarg;
			break;
		case ':':
			warnx("Option -%c requires an operand", optopt);
			update_usage(*argv, EXIT_FAILURE);
		case '?':
			warnx("Unrecognized option -%c", optopt);
			update_usage(*argv, EXIT_FAILURE);
		}
	}

	if(args.prefix == NULL) {
		args.prefix = "/hub";
	}

	if(argc - optind != 1) {
		update_usage(*argv, EXIT_FAILURE);
	}

	return args;
}

static void
update_shutdown(void) {
	state_deinit(&state);
}

int
main(int argc, char **argv) {
	const struct update_args args = update_parse_args(argc, argv);
	const char *uri = argv[optind];

	/* Create state context, if it encounters a pending snapshot, parses it as current or discards it */
	state_init(&state, args.prefix, args.flags, args.snapshots);
	atexit(update_shutdown);

	/* Annul or Apply previous unfinished update */
	update_consistency(&state);

	/* Fetch new snapshot, and update if necessary */
	update_perform(&state, uri);

	return EXIT_SUCCESS;
}

