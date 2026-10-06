/*
 * Thread-local storage for Mac OS X 10.4, which has none of its own.
 *
 * GCC then emulates `__thread`: every access calls
 * __emutls_get_address() with a small control object of the variable, and
 * libgcc's routine finds the thread's copy through pthread_getspecific().
 * Mesa looks up the current context and dispatch table that way on every
 * OpenGL call. This replaces libgcc's routine with the same interface and
 * a short cut for the thread that used it first, which in nearly every
 * program is the only one that draws: its table is reached through two
 * globals, without the pthread call. Other threads take the usual way.
 *
 * The control object's layout is libgcc's (emutls.c, struct
 * __emutls_object); the code is written for this project.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct emutls_object {
	uintptr_t size;
	uintptr_t align;
	union {
		uintptr_t offset;	/* 1-based slot; 0 until first use */
		void *ptr;
	} loc;
	void *templ;
};

/* A thread's copies, indexed by slot - 1. */
struct emutls_array {
	uintptr_t count;
	void **slot;
};

static pthread_mutex_t emutls_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_once_t emutls_once = PTHREAD_ONCE_INIT;
static pthread_key_t emutls_key;
static uintptr_t emutls_slots;

/* The short cut: the first thread's array, valid while that thread lives. */
static pthread_t fast_thread;
static struct emutls_array *volatile fast_array;

static void emutls_thread_exit(void *arg)
{
	struct emutls_array *a = arg;
	uintptr_t i;

	if (a == fast_array)
		fast_array = NULL;
	for (i = 0; i < a->count; i++)
		if (a->slot[i])
			/* The allocation starts one pointer before the copy. */
			free(((void **)a->slot[i])[-1]);
	free(a->slot);
	free(a);
}

static void emutls_init(void)
{
	pthread_key_create(&emutls_key, emutls_thread_exit);
}

static void *emutls_new_copy(struct emutls_object *obj)
{
	uintptr_t align = obj->align < sizeof(void *) ? sizeof(void *) : obj->align;
	void *raw = malloc(obj->size + align + sizeof(void *));
	void *copy;

	if (!raw)
		abort();
	copy = (void *)(((uintptr_t)raw + sizeof(void *) + align - 1) & ~(align - 1));
	((void **)copy)[-1] = raw;
	if (obj->templ)
		memcpy(copy, obj->templ, obj->size);
	else
		memset(copy, 0, obj->size);
	return copy;
}

static void *emutls_slow(struct emutls_object *obj)
{
	struct emutls_array *a;
	uintptr_t offset;

	pthread_once(&emutls_once, emutls_init);

	offset = obj->loc.offset;
	if (!offset) {
		pthread_mutex_lock(&emutls_lock);
		offset = obj->loc.offset;
		if (!offset)
			obj->loc.offset = offset = ++emutls_slots;
		pthread_mutex_unlock(&emutls_lock);
	}

	a = pthread_getspecific(emutls_key);
	if (!a) {
		a = calloc(1, sizeof(*a));
		if (!a)
			abort();
		pthread_setspecific(emutls_key, a);
		/* The first thread to come here gets the short cut. */
		pthread_mutex_lock(&emutls_lock);
		if (!fast_array && !fast_thread) {
			fast_thread = pthread_self();
			fast_array = a;
		}
		pthread_mutex_unlock(&emutls_lock);
	}
	if (offset > a->count) {
		/*
		 * Grown in place of the old table only for its own thread; the
		 * short cut reads `slot` and `count` from that thread alone.
		 */
		uintptr_t count = offset + 32;
		void **slot = realloc(a->slot, count * sizeof(*slot));

		if (!slot)
			abort();
		memset(slot + a->count, 0, (count - a->count) * sizeof(*slot));
		a->slot = slot;
		a->count = count;
	}
	if (!a->slot[offset - 1])
		a->slot[offset - 1] = emutls_new_copy(obj);
	return a->slot[offset - 1];
}

void *__emutls_get_address(void *object)
{
	struct emutls_object *obj = object;
	struct emutls_array *a = fast_array;
	uintptr_t offset = obj->loc.offset;

	if (a && offset && pthread_equal(pthread_self(), fast_thread) &&
	    offset <= a->count && a->slot[offset - 1])
		return a->slot[offset - 1];
	return emutls_slow(obj);
}

/* libgcc's other entry point: a common variable seen in several objects. */
void __emutls_register_common(void *object, unsigned int size,
			      unsigned int align, void *templ)
{
	struct emutls_object *obj = object;

	if (obj->size < size) {
		obj->size = size;
		obj->templ = NULL;
	}
	if (obj->align < align)
		obj->align = align;
	if (templ && size == obj->size)
		obj->templ = templ;
}
