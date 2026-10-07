/*
 * Thread-local storage for Mac OS X 10.4, which has none of its own.
 *
 * GCC then emulates `__thread`: every access calls
 * __emutls_get_address() with a small control object of the variable, and
 * libgcc's routine finds the thread's copy through pthread_getspecific().
 * Mesa looks up the current context and dispatch table that way on every
 * OpenGL call, twice with glthread, so in a game this routine runs a
 * million times a second.
 *
 * This replaces libgcc's routine with the same interface and a short cut
 * for the first few threads that use it, which in nearly every program are
 * the only ones that draw. A thread is recognised by where its stack is:
 * the stack pointer lies between two addresses noted when the thread first
 * came here. That takes no call at all (pthread_self() is three jumps away
 * through the C library, and asking it cost more than everything else
 * here), and the routine needs no stack frame of its own. A thread whose
 * stack pointer is found nowhere (more threads than slots, or code running
 * on a stack that is not the thread's own) takes the long way and is
 * still right.
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
#include <sys/resource.h>

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

/*
 * The short cut: the first few threads to come here, each from its first
 * time here until it ends.
 *
 * `low` and `high` are read by every thread and written by the slot's
 * own: a free slot has them at ~0 and 0, which no stack pointer lies
 * between; taking a slot writes `high` and then `low`, giving it up
 * `low` and then `high`, so that whatever another thread sees in between
 * is a range no stack pointer is in. The rest is the owner's alone: once
 * a thread has found its stack pointer in a slot, the slot is its own.
 */
#define FAST_THREADS 4
static struct emutls_fast {
	uintptr_t low, high;		/* the thread's stack: [low, high) */
	uintptr_t count;		/* of its array */
	void **slot;
	pthread_t thread;
	struct emutls_array *array;	/* NULL: the slot is free */
} fast[FAST_THREADS] = {
	{ ~(uintptr_t)0, 0, 0, NULL, 0, NULL },
	{ ~(uintptr_t)0, 0, 0, NULL, 0, NULL },
	{ ~(uintptr_t)0, 0, 0, NULL, 0, NULL },
	{ ~(uintptr_t)0, 0, 0, NULL, 0, NULL },
};

/* The largest stack the kernel gives the first thread (MAXSSIZ). */
#define MAIN_STACK_MOST (64u << 20)

/*
 * Where the calling thread's stack is. For the first thread the C library
 * of 10.4 answers with the size other threads get by default; its real
 * limit is the process's, below which the kernel keeps the addresses free
 * for it. Nothing (a range nothing is in) if the answer does not hold the
 * stack pointer: then the thread is not on the stack it was made with.
 */
static void emutls_stack(uintptr_t *low, uintptr_t *high)
{
	pthread_t self = pthread_self();
	uintptr_t top = (uintptr_t)pthread_get_stackaddr_np(self);
	uintptr_t size = pthread_get_stacksize_np(self);
	uintptr_t sp = (uintptr_t)__builtin_frame_address(0);
	struct rlimit limit;

	if (pthread_main_np() && !getrlimit(RLIMIT_STACK, &limit) &&
	    limit.rlim_cur > size && limit.rlim_cur <= MAIN_STACK_MOST)
		size = (uintptr_t)limit.rlim_cur;
	if (size <= top && sp >= top - size && sp < top) {
		*low = top - size;
		*high = top;
	} else {
		*low = ~(uintptr_t)0;
		*high = 0;
	}
}

static void emutls_thread_exit(void *arg)
{
	struct emutls_array *a = arg;
	uintptr_t i;

	for (i = 0; i < FAST_THREADS; i++)
		if (fast[i].array == a) {
			fast[i].low = ~(uintptr_t)0;
			__sync_synchronize();
			fast[i].high = 0;
			fast[i].count = 0;
			fast[i].slot = NULL;
			/* Last: from here on the slot may be taken again. */
			__sync_synchronize();
			fast[i].array = NULL;
		}
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

/* The long way: everything the short cut in __emutls_get_address() is not. */
static void *emutls_slow(struct emutls_object *obj) __attribute__((noinline));

static void *emutls_slow(struct emutls_object *obj)
{
	struct emutls_fast *mine = NULL;
	struct emutls_array *a = NULL;
	pthread_t self = pthread_self();
	uintptr_t offset, i;

	/* A thread with a slot that is not on its own stack just now. */
	for (i = 0; i < FAST_THREADS; i++)
		if (fast[i].array && fast[i].thread == self) {
			mine = &fast[i];
			a = mine->array;
			break;
		}

	pthread_once(&emutls_once, emutls_init);

	offset = obj->loc.offset;
	if (!offset) {
		pthread_mutex_lock(&emutls_lock);
		offset = obj->loc.offset;
		if (!offset)
			obj->loc.offset = offset = ++emutls_slots;
		pthread_mutex_unlock(&emutls_lock);
	}

	if (!a)
		a = pthread_getspecific(emutls_key);
	if (!a) {
		uintptr_t low, high;

		a = calloc(1, sizeof(*a));
		if (!a)
			abort();
		pthread_setspecific(emutls_key, a);
		/* The first threads to come here get the short cut. */
		emutls_stack(&low, &high);
		pthread_mutex_lock(&emutls_lock);
		for (i = 0; i < FAST_THREADS; i++)
			if (!fast[i].array) {
				mine = &fast[i];
				mine->thread = self;
				mine->array = a;
				mine->count = 0;
				mine->slot = NULL;
				mine->high = high;
				__sync_synchronize();
				mine->low = low;
				break;
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
	if (mine) {
		/* The table first: `count` says how much of it there is. */
		mine->slot = a->slot;
		mine->count = a->count;
	}
	return a->slot[offset - 1];
}

void *__emutls_get_address(void *object)
{
	struct emutls_object *obj = object;
	/* No variable has slot 0, and no table that many entries. */
	uintptr_t index = obj->loc.offset - 1;
	/* Read as it is: asking the compiler for it costs a stack frame. */
	register uintptr_t sp __asm__("r1");
	const struct emutls_fast *f;

	for (f = fast; f < fast + FAST_THREADS; f++)
		if (sp >= f->low && sp < f->high) {
			if (index < f->count) {
				void *copy = f->slot[index];

				if (copy)
					return copy;
			}
			break;
		}
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
