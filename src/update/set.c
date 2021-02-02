/*
	set.c
	Copyright (c) 2021, Valentin Debon

	This file is part of the update program
	subject the BSD 3-Clause License, see LICENSE
*/
#include "set.h"

#include <stdlib.h>
#include <string.h>
#include <syslog.h>

/* TODO: Benchmark array against hash table
 * manipulating strings requires memory locality,
 * as do all algorithms here. An update shouldn't bring algorithmic
 * problems up to some threshold. A good benchmark should be done
 * to determine whether it does.
 */

#define SET_DEFAULT_CAPACITY 1024

/* FNV hash is extremely basic to implement */
#define FNV_OFFSET_BASIS 0xCBF29CE484222325	
#define FNV_PRIME        0x100000001B3

/****************
 * String Class *
 ****************/

static int
compare_string(const void *lhs, const void *rhs) {
	return strcmp(lhs, rhs);
}

static hash_t
hash_string(const void *element) {
	const uint8_t *data = (const uint8_t *)element;
	const uint8_t * const end = data + strlen(element);
	hash_t hash = FNV_OFFSET_BASIS;

	while(data != end) {
		hash *= FNV_PRIME;
		hash ^= *data;

		data++;
	}

	return hash;
}

static size_t
size_string(const void *element) {
	return strlen(element) + 1;
}

const struct set_class string_set_class = {
	.compare_function = compare_string,
	.hash_function = hash_string,
	.size_function = size_string,
};

/**************
 * Pair Class *
 **************/

static size_t
size_pair(const void *element) {
	const size_t keysize = strlen(element) + 1;

	return keysize + strlen((const char *)element + keysize) + 1;
}

const struct set_class pair_set_class = {
	.compare_function = compare_string,
	.hash_function = hash_string,
	.size_function = size_pair,
};

/********************
 * Public functions *
 ********************/

void
set_init(struct set *set, const struct set_class *set_class) {
	set->class = set_class;
	set->capacity = 0;
	set->size = 0;
	set->elements = NULL;
}

void
set_deinit(struct set *set) {
	free(set->elements);
}

bool
set_find(const struct set *set, const void *element, const void **foundp) {
	const struct set_class *class = set->class;
	const void *current = set->elements,
		* const end = (const uint8_t *)set->elements + set->size;

	while(current != end) {

		/* Two equal elements could possibly not have the same size, we cannot rely on such assumption */
		if(class->compare_function(element, current) == 0) {
			if(foundp != NULL) {
				*foundp = current;
			}
			break;
		}

		current = (const uint8_t *)current + class->size_function(current);
	}

	return current != end;
}

bool
set_insert(struct set *set, const void *element) {
	if(!set_find(set, element, NULL)) {
		const struct set_class *class = set->class;

		/* While the value cannot fit, update size */
		const size_t elementsize = class->size_function(element);
		while(set->capacity - set->size < elementsize) {
			const size_t newcapacity = set->capacity == 0 ? SET_DEFAULT_CAPACITY : set->capacity * 2;
			void *newelements = realloc(set->elements, newcapacity);

			if(newelements == NULL) {
				syslog(LOG_ERR, "set_insert: Element of %lu bytes: %m", elementsize);
				exit(EXIT_FAILURE);
			}

			set->capacity = newcapacity;
			set->elements = newelements;
		}

		/* Append the value at the end */
		memcpy((uint8_t *)set->elements + set->size, element, elementsize);

		set->size += elementsize;

		return true;
	} else { /* Already in the set, not inserted */
		return false;
	}
}

bool
set_remove(struct set *set, const void *element) {
	const struct set_class *class = set->class;

	/* First, we try to find the element in the set */
	void *current = set->elements,
		* const end = (uint8_t *)set->elements + set->size;
	size_t currentsize;

	while(current != end) {
		currentsize = class->size_function(current);

		if(class->compare_function(element, current) == 0) {
			break;
		}

		current = (uint8_t *)current + currentsize;
	}

	/* If we found it, remove it */
	if(current != end) {
		const void *nextelements = (uint8_t *)current + currentsize;
		const size_t nextelementssize = (const uint8_t *)end - (const uint8_t *)nextelements;

		memmove(current, nextelements, nextelementssize);

		set->size -= currentsize;

		return true;
	} else { /* Already not in the set, nothing to remove */
		return false;
	}
}

void
set_iterator_init(struct set_iterator *iterator, const struct set *set) {
	iterator->size_function = set->class->size_function;
	iterator->left = set->size;
	iterator->next = set->elements;
}

bool
set_iterator_next(struct set_iterator *iterator, const void **elementp, size_t *sizep) {
	if(iterator->left != 0) {
		const void *element = iterator->next;
		size_t size = iterator->size_function(element);

		*elementp = element;
		*sizep = size;

		iterator->next = (const uint8_t *)iterator->next + size;
		iterator->left -= size;

		return true;
	} else {
		return false;
	}
}

