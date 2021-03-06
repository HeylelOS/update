/*
	schemes/file.c
	Copyright (c) 2021, Valentin Debon

	This file is part of the update program
	subject the BSD 3-Clause License, see LICENSE
*/
#include "file.h"

#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#define FILE_SCHEME_SNAPSHOT_FILE      "snapshot"
#define FILE_SCHEME_PACKAGES_DIRECTORY "packages"

static struct {
	const char *path;
	int dirfd;
} scheme;

void
file_scheme_open(const struct state *state, const char *uri) {
	static const char authorityprefix[] = "file://";
	if(strncmp(authorityprefix, uri, sizeof(authorityprefix) - 1) != 0) {
		syslog(LOG_ERR, "file_scheme_open: Invalid uri for file scheme, between scheme and authority: %s: %m", uri);
		exit(EXIT_FAILURE);
	}

	scheme.path = uri + sizeof(authorityprefix) - 1;
	scheme.dirfd = open(scheme.path, O_RDONLY | O_DIRECTORY);
	if(scheme.dirfd < 0) {
		syslog(LOG_ERR, "file_scheme_open: Unable to open scheme %s: %m", uri);
		exit(EXIT_FAILURE);
	}
}

void
file_scheme_snapshot(const struct state *state) {
	/* Open snapshot file */
	int fd = openat(scheme.dirfd, FILE_SCHEME_SNAPSHOT_FILE, O_RDONLY);
	if(fd < 0) {
		syslog(LOG_ERR, "file_scheme_snapshot: Unable to open snapshot file at %s/" FILE_SCHEME_SNAPSHOT_FILE ": %m", scheme.path);
		exit(EXIT_FAILURE);
	}

	/* Determine size for read and allocation */
	struct stat st;
	if(fstat(fd, &st) != 0) {
		syslog(LOG_ERR, "file_scheme_snapshot: Unable to stat snapshot file at %s/" FILE_SCHEME_SNAPSHOT_FILE ": %m", scheme.path);
		exit(EXIT_FAILURE);
	}

	if(st.st_size == 0) {
		syslog(LOG_ERR, "file_scheme_snapshot: Invalid size for snapshot file at %s/" FILE_SCHEME_SNAPSHOT_FILE ": %m", scheme.path);
		exit(EXIT_FAILURE);
	}

	/* Allocate buffer for read/write */
	char * const buffer = malloc(st.st_size);
	if(buffer == NULL) {
		syslog(LOG_ERR, "file_scheme_snapshot: Unable to allocate snapshot buffer file (%lu bytes): %m", st.st_size);
		exit(EXIT_FAILURE);
	}

	/* Read the whole source file */
	size_t pagesize = getpagesize(), left = st.st_size;
	char *current = buffer;
	ssize_t readval;

	while(readval = read(fd, current, pagesize), readval > 0) {
		current += readval;
		left -= readval;
	}

	if(readval == -1) {
		syslog(LOG_ERR, "file_scheme_snapshot: Unable to read snapshot file at %s/" FILE_SCHEME_SNAPSHOT_FILE ": %m", scheme.path);
		exit(EXIT_FAILURE);
	}

	close(fd);

	/* Source file completely read in buffer, opening pending */
	fd = openat(state->dirfd, STATE_SNAPSHOT_PENDING, O_CREAT | O_WRONLY | O_TRUNC);
	if(fd < 0) {
		syslog(LOG_ERR, "file_scheme_snapshot: Unable to create " STATE_SNAPSHOT_PENDING " snapshot file: %m");
		exit(EXIT_FAILURE);
	}

	/* Write the pending snapshot, hoping the filesystem is transactional on writes */
	const ssize_t writeval = write(fd, buffer, st.st_size);
	if(writeval != st.st_size) {
		if(writeval == -1) {
			syslog(LOG_ERR, "file_scheme_snapshot: Unable to write " STATE_SNAPSHOT_PENDING " snapshot: %m");
		exit(EXIT_FAILURE);
		} else {
			syslog(LOG_ERR, "file_scheme_snapshot: Unable to write whole snapshot at %s/" FILE_SCHEME_SNAPSHOT_FILE " in " STATE_SNAPSHOT_PENDING " snapshot: %m", scheme.path);
		exit(EXIT_FAILURE);
		}
	}

	close(fd);

	/* Don't forget to deallocate the buffer */
	free(buffer);
}

void
file_scheme_packages(const struct state *state, const struct set *packages) {
	/* Open packages directory */
	int packagesdirfd = openat(scheme.dirfd, FILE_SCHEME_PACKAGES_DIRECTORY , O_RDONLY | O_DIRECTORY);
	if(packagesdirfd < 0) {
		syslog(LOG_ERR, "file_scheme_packages: Unable to open packages directory at %s/" FILE_SCHEME_PACKAGES_DIRECTORY ": %m", scheme.path);
		exit(EXIT_FAILURE);
	}

	/* Iterate every packages */
	struct set_iterator packagesiterator;

	set_iterator_init(&packagesiterator, packages);

	const void *element;
	size_t elementsize;
	while(set_iterator_next(&packagesiterator, &element, &elementsize)) {
		const char * const package = element;
		/* Open package file */
		const int fd = openat(packagesdirfd, package, O_RDONLY);
		struct hny_extraction *extraction;

		if(packagesdirfd < 0) {
			syslog(LOG_ERR, "file_scheme_packages: Unable to open packages directory at %s/" FILE_SCHEME_PACKAGES_DIRECTORY ": %m", scheme.path);
		exit(EXIT_FAILURE);
		}

		/* Create extraction handler */
		int errcode = hny_extraction_create(&extraction, state->hny, package);
		if(errcode != 0) {
			syslog(LOG_ERR, "file_scheme_packages: Unable to create extraction: %s", strerror(errcode));
		exit(EXIT_FAILURE);
		}

		/* Extract everything */
		char buffer[getpagesize()];
		enum hny_extraction_status status;
		ssize_t readval;
		while((readval = read(fd, buffer, sizeof(buffer))) > 0
			&& (status = hny_extraction_extract(extraction, buffer, readval, &errcode))
				== HNY_EXTRACTION_STATUS_OK);

		/* Handle errors */
		if(readval == -1) {
			syslog(LOG_ERR, "file_scheme_packages: Unable to read from package '%s': %m", package);
		exit(EXIT_FAILURE);
		} else if(HNY_EXTRACTION_STATUS_IS_ERROR(status)) {
			if(HNY_EXTRACTION_STATUS_IS_ERROR_XZ(status)) {
				syslog(LOG_ERR, "file_scheme_packages: Unable to extract '%s', error while uncompressing", package);
		exit(EXIT_FAILURE);
			} else if(HNY_EXTRACTION_STATUS_IS_ERROR_CPIO(status)) {
				if(HNY_EXTRACTION_STATUS_IS_ERROR_CPIO_SYSTEM(status)) {
					syslog(LOG_ERR, "file_scheme_packages: Unable to extract '%s', system error while unarchiving: %s", package, strerror(errcode));
		exit(EXIT_FAILURE);
				} else {
					syslog(LOG_ERR, "file_scheme_packages: Unable to extract '%s', error while unarchiving", package);
		exit(EXIT_FAILURE);
				}
			} else {
				syslog(LOG_ERR, "file_scheme_packages: Unable to extract '%s', archive not finished", package);
		exit(EXIT_FAILURE);
			}
		}

		/* Don't forget to close */
		hny_extraction_destroy(extraction);
		close(fd);
	}

	set_iterator_deinit(&packagesiterator);

	/* Don't forget to close packages directory */
	close(packagesdirfd);
}

void
file_scheme_close(const struct state *state) {
	close(scheme.dirfd);
}

