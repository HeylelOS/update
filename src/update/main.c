/*
	main.c
	Copyright (c) 2021, Valentin Debon

	This file is part of the update program
	subject the BSD 3-Clause License, see LICENSE
*/
#include "check.h"
#include "fetch.h"
#include "apply.h"
#include "annul.h"
#include "state.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <syslog.h>
#include <unistd.h>
#include <stdnoreturn.h>

struct update_args {
	char *prefix;
	char *snapshots;
	unsigned consistencyonly : 1;
	int flags;
};

static struct state state;

static void
update_sigterm(int signo) {
	state.shouldexit = true;
}

/* To most probably avoid corruption, we specify
 * a sigterm handler to notify the state should exit.
 * This is to avoid cases where the process is interrupted by init,
 * or an interactive user Ctrl+Cing the process in a console.
 * Obviously it cannot handle a SIGKILL or other signals defaulting to termination.
 * This is basically mitigation, in the hope users aren't too dumbs to kill -9 the process. */
static void
update_protect_termination(bool isinteractive) {
	const struct sigaction action = {
		.sa_handler = update_sigterm,
		.sa_flags = SA_RESTART,
	};

	sigaction(SIGTERM, &action, NULL);
	if(isinteractive) {
		sigaction(SIGINT, &action, NULL);
	}
}

static void
update_consistency(struct state *state) {

	syslog(LOG_INFO, "Consistency check for prefix at: %s", hny_path(state->hny));

	/* If pending snapshot hasn't been committed */
	if(!check_pending(state)) {
		struct set newgeister, newpackages;

		syslog(LOG_INFO, "Found previous pending snapshot, trying recovery...");

		set_init(&newgeister, &pair_set_class);
		set_init(&newpackages, &string_set_class);
		state_diff(state, &newgeister, &newpackages);

		/* Check if at least one of the new geister was installed, if not,
		 * we cannot guarantee all packages where fetched,
		 * and we should remove them all. */
		const bool allnewpackagesfetched = check_new_geister(state, &newgeister);
		annul_new_geister(state, &newgeister, &newpackages);

		if(allnewpackagesfetched) {
			syslog(LOG_INFO, "All packages were fetched, applying previous pending snapshot.");
			apply_new_geister(state, &newgeister, &newpackages);
			apply_pending(state);
		} else {
			/* Uncommitted packages will be removed during cleanup */
			syslog(LOG_INFO, "No pending geist found, reverting pending snapshot.");
			annul_pending(state);
		}

		set_deinit(&newgeister);
		set_deinit(&newpackages);
	}

	/* Remove all deprecated packages/geister,
	 * whether they're old ones, or uncommitted new. */
	apply_cleanup(state);

	syslog(LOG_INFO, "Finished consistency check.");
}

static void
update_perform(struct state *state, const char *uri) {
	struct set newgeister, newpackages;

	/******************
	 * Fetch sequence *
	 ******************/

	syslog(LOG_INFO, "Fetching update from: %s", uri);

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

	syslog(LOG_INFO, "Fetch sequence finished, applying modifications.");

	/* New geister are shifted, deprecated geister/packages are cleaned */
	apply_new_geister(state, &newgeister, &newpackages);

	set_deinit(&newgeister);
	set_deinit(&newpackages);

	/* The pending snapshot is commited */
	apply_pending(state);

	/* The prefix is cleaned up if dirty */
	apply_cleanup(state);

	syslog(LOG_INFO, "Finished performing update.");
}

static void noreturn
update_usage(const char *updatename, int status) {
	fprintf(stderr, "usage: %s [-hb] [-p <prefix>] [-s <snapshots>] <uri>\n"
	                "       %s -C [-hb] [-p <prefix>] [-s <snapshots>]\n",
		updatename, updatename);
	exit(status);
}

static struct update_args
update_parse_args(int argc, char **argv) {
	struct update_args args = {
		.prefix = getenv("HNY_PREFIX"),
		.snapshots = "/data/update",
		.consistencyonly = 0,
		.flags = 0,
	};
	int c;

	while((c = getopt(argc, argv, ":hbCp:s:")) != -1) {
		switch(c) {
		case 'h':
			update_usage(*argv, EXIT_SUCCESS);
		case 'b':
			args.flags |= HNY_FLAGS_BLOCK;
			break;
		case 'C':
			args.consistencyonly = 1;
			break;
		case 'p':
			args.prefix = optarg;
			break;
		case 's':
			args.snapshots = optarg;
			break;
		case ':':
			fprintf(stderr, "Option -%c requires an operand\n", optopt);
			update_usage(*argv, EXIT_FAILURE);
		case '?':
			fprintf(stderr, "Unrecognized option -%c\n", optopt);
			update_usage(*argv, EXIT_FAILURE);
		}
	}

	if(args.prefix == NULL) {
		args.prefix = "/hub";
	}

	if(argc - optind != !args.consistencyonly) {
		update_usage(*argv, EXIT_FAILURE);
	}

	return args;
}

static void
update_shutdown(void) {
	state_deinit(&state);
	closelog();
}

int
main(int argc, char **argv) {
	const struct update_args args = update_parse_args(argc, argv);
	const bool isinteractive = isatty(STDOUT_FILENO) != 0;
	const char *uri = argv[optind];

	/* Open system log */
	openlog("update", isinteractive ? LOG_CONS | LOG_PERROR : 0, LOG_USER);
	update_protect_termination(isinteractive);

	/* Create state context, if it encounters a pending snapshot, parses it as current or discards it */
	state_init(&state, args.prefix, args.flags, args.snapshots);
	atexit(update_shutdown);

	/* Annul or Apply previous unfinished update */
	update_consistency(&state);

	/* Fetch new snapshot, and update if necessary */
	if(args.consistencyonly == 0) {
		update_perform(&state, uri);
	}

	return EXIT_SUCCESS;
}

