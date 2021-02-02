/*
	set.h
	Copyright (c) 2021, Valentin Debon

	This file is part of the update program
	subject the BSD 3-Clause License, see LICENSE
*/
#ifndef UPDATE_SET_H
#define UPDATE_SET_H

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>

typedef uint64_t hash_t;

struct set_class {
	int    (*compare_function)(const void *, const void *);
	hash_t (*hash_function)(const void *);
	size_t (*size_function)(const void *);
};

struct set {
	const struct set_class *class;
	size_t capacity;
	size_t size;
	void *elements;
};

struct set_iterator {
	size_t (*size_function)(const void *);
	size_t left;
	const void *next;
};

extern const struct set_class pair_set_class;
extern const struct set_class string_set_class;

void
set_init(struct set *set, const struct set_class *set_class);

void
set_deinit(struct set *set);

#define set_is_empty(set) ((set)->size == 0)

bool
set_find(const struct set *set, const void *element, const void **foundp);

#define set_empty(set) (set)->size = 0

bool
set_insert(struct set *set, const void *element);

bool
set_remove(struct set *set, const void *element);

void
set_iterator_init(struct set_iterator *iterator, const struct set *set);

#define set_iterator_deinit(it) (void)(it)

bool
set_iterator_next(struct set_iterator *iterator, const void **elementp, size_t *sizep);

/* UPDATE_SET_H */
#endif
