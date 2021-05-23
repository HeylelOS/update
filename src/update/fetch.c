/*
	fetch.c
	Copyright (c) 2021, Valentin Debon

	This file is part of the update program
	subject the BSD 3-Clause License, see LICENSE
*/
#include "fetch.h"

#include "config.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <syslog.h>

#include <curl/curl.h>
#include <hny.h>

struct snapshot_buffer {
	size_t capacity, count;
	void *data;
};

static size_t
snapshot_buffer_write(const void *data, size_t one, size_t count, struct snapshot_buffer *buffer) {
	while(buffer->capacity - buffer->count < count) {
		const size_t newcapacity = buffer->capacity * 2;
		void *newdata = realloc(buffer->data, newcapacity);

		if(newdata == NULL) {
			return 0;
		}

		buffer->capacity = newcapacity;
		buffer->data = newdata;
	}

	memcpy((uint8_t *)buffer->data + buffer->count, data, count);

	buffer->count += count;

	return count;
}

static size_t
package_write(const void *data, size_t one, size_t count, struct hny_extraction *extraction) {
}

static struct {
	CURLM *multi;
	CURLU *url;
} curl;

static void
fetch_curl_run(const struct state *state, int *runningp) {
	/* CURL's timeout */
	struct timespec timeout;
	long multitimeout;

	if(curl_multi_timeout(curl.multi, &multitimeout) != CURLE_OK) {
		syslog(LOG_ERR, "fetch_curl_run: curl_multi_timeout");
		exit(EXIT_FAILURE);
	}

	if(multitimeout > 0) {
		timeout.tv_sec = multitimeout / 1000;
		timeout.tv_nsec = (multitimeout % 1000) * 1000000;
	} else {
		timeout.tv_sec = 0;
		timeout.tv_nsec = 0;
	}

	/* CURL's fdsets */
	fd_set readset, writeset, exceptset;
	int maxfd;

	FD_ZERO(&readset);
	FD_ZERO(&writeset);
	FD_ZERO(&exceptset);

	if(curl_multi_fdset(curl.multi, &readset, &writeset, &exceptset, &maxfd) != CURLE_OK) {
		syslog(LOG_ERR, "fetch_curl_run: curl_multi_fdset");
		exit(EXIT_FAILURE);
	}

	/* CURLs timeout, if no filedescriptors */
	if(maxfd == -1) {
		/* If there are no filedescriptors, curl_multi_fdset() doc suggests wait 100ms */
		if(timeout.tv_sec > 0 || timeout.tv_nsec > 100000000) {
			timeout.tv_sec = 0;
			timeout.tv_nsec = 100000000;
		}
	}

	/* Select */
	if(pselect(maxfd + 1, &readset, &writeset, &exceptset, &timeout, NULL) == -1) {
		syslog(LOG_ERR, "fetch_curl_run: pselect: %m");
		exit(EXIT_FAILURE);
	}

	/* Perform */
	if(curl_multi_perform(curl.multi, runningp) != CURLE_OK) {
		syslog(LOG_ERR, "fetch_curl_run: curl_multi_perform");
		exit(EXIT_FAILURE);
	}
}

void
fetch_open(const struct state *state, const char *uri) {

	curl.multi = curl_multi_init();

	curl.url = curl_url();
	const CURLUcode ucode = curl_url_set(curl.url, CURLUPART_URL, uri, 0);
	if(ucode != CURLUE_OK) {
		syslog(LOG_ERR, "fetch_open: Invalid URI %s", uri);
		exit(EXIT_FAILURE);
	}

	if(state->shouldexit) {
		exit(EXIT_SUCCESS);
	}
}

void
fetch_snapshot(const struct state *state) {
	/* Initialize the buffer */
	struct snapshot_buffer buffer = {
		.capacity = CONFIG_DEFAULT_FETCH_SNAPSHOT_BUFFER_CAPACITY,
		.count = 0,
		.data = malloc(buffer.capacity),
	};

	if(buffer.data == NULL) {
		syslog(LOG_ERR, "fetch_snapshot: Unable to allocate memory for buffer");
		exit(EXIT_FAILURE);
	}

	/* Initialize the CURL easy handle (and URL) */
	CURL * const easy = curl_easy_init();
	CURLU *snapshoturl = curl_url_dup(curl.url);
	char *snapshotpath;
	CURLUcode ucode;

	ucode = curl_url_set(snapshoturl, CURLUPART_URL, "snapshot", 0);
	if(ucode != CURLE_OK) {
		syslog(LOG_ERR, "fetch_snapshot: Unable to redirect url to snapshot");
		exit(EXIT_FAILURE);
	}

	ucode = curl_url_get(snapshoturl, CURLUPART_URL, &snapshotpath, 0);
	if(ucode != CURLE_OK) {
		syslog(LOG_ERR, "fetch_snapshot: Unable to get snapshot redirect url");
		exit(EXIT_FAILURE);
	}

	curl_easy_setopt(easy, CURLOPT_URL, snapshotpath);
	curl_easy_setopt(easy, CURLOPT_WRITEDATA, &buffer);
	curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, snapshot_buffer_write);

	curl_free(snapshotpath);
	curl_url_cleanup(snapshoturl);

	/* Performing request */
	int running;

	curl_multi_add_handle(curl.multi, easy);

	do {
		fetch_curl_run(state, &running);
	} while(running != 0);

	curl_multi_remove_handle(curl.multi, easy);
	curl_easy_cleanup(easy);

	/* Pending snapshot in memory */
	const int fd = openat(state->dirfd, STATE_SNAPSHOT_PENDING, O_CREAT | O_TRUNC, 0644);
	const size_t writeval = write(fd, buffer.data, buffer.count);

	if(writeval != buffer.count) {
		syslog(LOG_ERR, "fetch_snapshot: Unable to atomically write to pending snapshot (%lu out of %lu)", buffer.count, writeval);
		exit(EXIT_FAILURE);
	}

	close(fd);

	free(buffer.data);

	if(state->shouldexit) {
		exit(EXIT_SUCCESS);
	}
}

void
fetch_new_packages(const struct state *state, const struct set *newpackages) {
	if(state->shouldexit) {
		exit(EXIT_SUCCESS);
	}
}

void
fetch_close(const struct state *state) {

	curl_url_cleanup(curl.url);

	curl_multi_cleanup(curl.multi);

	if(state->shouldexit) {
		exit(EXIT_SUCCESS);
	}
}

