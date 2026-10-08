#!/usr/bin/env python3
# Generate the glue between Apple's GL dispatch table and Mesa.
#
#   gen_dispatch.py <gliDispatch.h> <output.c>
#
# Apple's public header (in the 10.4u SDK) declares the table as a structure
# of function pointers, each taking the engine's context first:
#     void (*clear_color)(GLIContext ctx, GLclampf red, ...);
# For every entry whose OpenGL function Mesa has (clear_color is
# glClearColor), this writes a function with the table's signature that
# makes the right Mesa context current and calls Mesa's, plus the code that
# looks Mesa's functions up and puts ours in a table. The output is built
# from Apple's header at build time and is not kept in the repository.
#
# The entries Mesa has no function for (Apple's own extensions) stay the
# engine's. To see what programs do with them, every entry also gets a
# wrapper with the same signature that counts the call, logs the first few
# and passes it on to the engine's function. Those are put in the table
# only while the bundle's log is on (rdn_logging), and only where Mesa's
# function is missing.
#
# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT

import re
import sys

ENTRY = re.compile(r'^\s*(.+?)\(\*\s*([A-Za-z0-9_]+)\s*\)\s*\(\s*GLIContext\s+ctx\s*(?:,\s*(.*?))?\)\s*;', re.S)


# Entries whose name in Apple's table is not the function's name.
ALIASES = {
    'enable_vertex_attrib_ARB': 'glEnableVertexAttribArrayARB',
    'disable_vertex_attrib_ARB': 'glDisableVertexAttribArrayARB',
    'bind_vertex_array_EXT': 'glBindVertexArray',
    'delete_vertex_arrays_EXT': 'glDeleteVertexArrays',
    'gen_vertex_arrays_EXT': 'glGenVertexArrays',
    'is_vertex_array_EXT': 'glIsVertexArray',
}


def gl_name(entry):
    """clear_color -> glClearColor, tex_image2D -> glTexImage2D."""
    if entry in ALIASES:
        return ALIASES[entry]
    return 'gl' + ''.join(p[:1].upper() + p[1:] for p in entry.split('_'))


def arg_names(params):
    names = []
    for p in params.split(','):
        m = re.search(r'([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[[^\]]*\])?\s*$', p)
        if not m:
            return None
        names.append(m.group(1))
    return names


def trace_call(name, params, names, before='', after='', more=''):
    """The rdn_log call that prints one GL call with its arguments."""
    fmt, args = [], []
    for p, n in zip(params.split(',') if params else [], names):
        if '*' in p or '[' in p:
            fmt.append('%p')
            args.append('(void *)%s' % n)
        elif re.search(r'GL(float|clampf|double|clampd)\b', p):
            fmt.append('%g')
            args.append('(double)%s' % n)
        else:
            fmt.append('0x%lx')
            args.append('(unsigned long)%s' % n)
    return 'rdn_log("%s%s(%s)%s"%s%s)' % (before, gl_name(name), ', '.join(fmt),
                                          after,
                                          ''.join(', ' + a for a in args), more)


# Entries the bundle looks at as they go by, in the window server only
# (rdn_watch): how it draws the place of another program's surface.
WATCHED = ('ortho', 'begin', 'end', 'vertex2f', 'tex_coord2f', 'color4ub',
           'enable', 'disable', 'active_texture', 'bind_texture',
           'tex_image2D', 'tex_sub_image2D', 'tex_parameterf')

# Apple-only extensions that Mesa does not have and that Apple's own
# programs use without asking (the window server does): the entry, the
# argument that names the parameter, and the values of it to accept and
# drop before Mesa sees them and records GL_INVALID_ENUM.
#   0x85B2 GL_UNPACK_CLIENT_STORAGE_APPLE: Mesa copies texture data anyway.
#   0x85BC GL_TEXTURE_STORAGE_HINT_APPLE, 0x85B1 GL_TRANSFORM_HINT_APPLE:
#   hints.
# GL_ARB_vertex_blend, the part of it that programs with vertex programs
# use. Mesa has none of the extension; every Mac renderer of Tiger's time
# has it, and World of Warcraft 1.12 uses it without asking: it gives the
# bone weights of its models with glWeightPointerARB and reads them in its
# vertex programs as vertex.weight (all of its models were missing).
# GL_ARB_vertex_program defines vertex.weight as generic attribute 1, so:
#   - vertex.weight and vertex.weight[0] in a vertex program's text become
#     vertex.attrib[1] before Mesa compiles it (weight_as_attrib),
#   - glWeightPointerARB is glVertexAttribPointerARB for attribute 1,
#     integers normalized as the extension says (OWN),
#   - GL_WEIGHT_ARRAY_ARB in glEnableClientState and glDisableClientState
#     is attribute array 1 (CLIENT_STATE).
# Not done: glWeight*vARB, glVertexBlendARB and the blending of several
# modelview matrices without a vertex program. Those stay the engine's.
WEIGHT = """\
static char *weight_as_attrib(GLenum target, const char *text, GLsizei *len)
{
	static const char from[] = "vertex.weight", to[] = "vertex.attrib[1]";
	const GLsizei flen = sizeof(from) - 1, tlen = sizeof(to) - 1;
	GLsizei i, n = 0, o = 0;
	char *out;

	if (target != 0x8620 || !text)	/* GL_VERTEX_PROGRAM_ARB */
		return NULL;
	for (i = 0; i + flen <= *len; i++)
		if (text[i] == 'v' && !memcmp(text + i, from, flen))
			n++;
	if (!n)
		return NULL;
	out = malloc(*len + n * (tlen - flen) + 1);
	if (!out)
		return NULL;
	for (i = 0; i < *len; ) {
		if (i + flen <= *len && text[i] == 'v' && !memcmp(text + i, from, flen)) {
			memcpy(out + o, to, tlen);
			o += tlen;
			i += flen;
			if (i + 3 <= *len && !memcmp(text + i, "[0]", 3))
				i += 3;
		} else
			out[o++] = text[i++];
	}
	out[o] = 0;
	*len = o;
	return out;
}
"""

# Entries Mesa has no function for that are ours instead of the engine's:
# the body, after the context is made current.
OWN = {
    'weight_pointer_ARB':
        '\tm_vertex_attrib_pointer_ARB(1, size, type, type != 0x1406 && type != 0x140A,\n'
        '\t\t\t\t    stride, pointer);',
}

# glEnableClientState and glDisableClientState: the Mesa function that
# takes GL_WEIGHT_ARRAY_ARB (0x86AD) as attribute array 1.
CLIENT_STATE = {
    'enable_client_state': 'm_enable_vertex_attrib_array_ARB',
    'disable_client_state': 'm_disable_vertex_attrib_array_ARB',
}

# GL_APPLE_vertex_array_range (with GL_APPLE_fence), which Mesa lacks. A
# program that has it keeps its vertices in its own memory, says which
# memory (glVertexArrayRangeAPPLE) and when it changed some of it
# (glFlushVertexArrayRangeAPPLE), and draws from it with ordinary array
# pointers; Apple's drivers let the GPU read that memory. Without it Mesa
# copies, for every draw, the vertices from the lowest to the highest index
# the draw uses: Call of Duty 2 has no other way of drawing, its draws of
# the world reach across a 20 MB buffer each, and every one of its vertex
# array objects keeps Mesa's last copy alive (out of video memory after
# 1.2 GB of such copies, and 0.4 frames a second on the way there).
#
# Here the memory gets a copy in a buffer object (a "mirror"): of the whole
# malloc block the range lies in when that can be found, so that all the
# ranges a program names inside one of its buffers share one copy, else of
# the range itself. A flush updates every mirror it touches, with the parts
# that differ from what the mirror was last given. An array
# pointer into a mirror becomes an offset into its buffer object. What the
# program changes without a flush is not seen: that is the extension's
# rule, and the only way this differs from the GPU reading the memory.
#
# The fences only ever said when the GPU had read the program's memory;
# with copies it always has, so they are always finished.
#
# For the programs named in RDN_VAR_LIST, one name a line, or with
# RDN_VAR=1 in the environment (0: not even if named). A "+" before the
# name, or RDN_VAR=2: without the copies where that can be (VAR_USERS). Never asked for by
# name by Call of Duty 2: it takes the range for granted and looks for
# GL_APPLE_vertex_array_object, GL_APPLE_fence and GL_APPLE_element_array
# before it calls any of this, so those are named too (it never calls the
# last one's functions, and nothing here implements them).
VAR_HELP = """
#define RDN_VAR_LIST "/Library/Application Support/RadeonNI/vertexrange"

/* 0: not ours; 1: with copies; 2: without, where that can be (below). */
static int var_on(void)
{
	static int on = -1;
	const char *env, *name = getprogname();
	char line[256];
	FILE *f;

	if (on >= 0)
		return on;
	on = 0;
	if (!name || !strcmp(name, "WindowServer") || !m_gen_buffers || !m_buffer_sub_data)
		return on;
	if ((env = getenv("RDN_VAR")) != NULL)
		on = atoi(env) < 0 ? 0 : atoi(env) > 2 ? 2 : atoi(env);
	else if ((f = fopen(RDN_VAR_LIST, "r")) != NULL) {
		while (fgets(line, sizeof(line), f)) {
			line[strcspn(line, "\\r\\n")] = 0;
			if (!strcmp(line, name))
				on = 1;
			else if (line[0] == '+' && !strcmp(line + 1, name))
				on = 2;
		}
		fclose(f);
	}
	if (on)
		rdn_log("GL_APPLE_vertex_array_range and GL_APPLE_fence: ours, for %s%s", name,
			on == 2 ? ", the GPU reading the program's memory where it can" : "");
	return on;
}

struct var_mirror {
	const char *base;
	size_t size;
	GLuint buffer;
	unsigned refs;		/* vertex array objects that point into it */
	char *shadow;		/* what the buffer object has, if memory allowed */
	const char *dirty, *dirty_end;	/* flushed, not yet brought up to date */
	unsigned long seen, sent;	/* bytes compared with the shadow, and sent */
	/*
	 * No copy: the buffer object is over the program's memory itself
	 * (GL_AMD_pinned_memory), and `user` is its place in var_user + 1.
	 */
	unsigned short user;
	unsigned char gone;	/* let go, but a vertex array object still has it */
	unsigned char stale;	/* the program freed the memory it was over */
	unsigned char changed;	/* a flush has found it different from the copy */
	unsigned used;		/* var_swaps when last flushed or pointed into */
};

#define VAR_VAO_MIRRORS 4
struct var_vao {
	struct var_mirror *mirror[VAR_VAO_MIRRORS];
	unsigned char next;
	/* The range flushed last with it bound, and the draw count then. */
	struct var_mirror *in;
	const char *ptr;
	size_t len;
	unsigned at;
};

static struct {
	void *ctx;			/* one context draws; another starts over */
	struct var_mirror **mirrors;
	unsigned count, room;
	struct var_vao *vaos;
	unsigned vao_room;
	GLuint vao;			/* the one bound */
	const char *range;		/* glVertexArrayRangeAPPLE's last */
	size_t range_size;
	unsigned long bytes, copied, freed, revived;
} var;
static unsigned var_swaps;
/* The mirrors with something flushed that they have not been given yet. */
#define VAR_DIRTY 16
static struct var_mirror *var_dirty[VAR_DIRTY];
static unsigned var_dirty_count;
static void var_settle(void *ctx);
/*
 * The large blocks malloc has freed or handed out lately (var_moving):
 * a program's sign that it has other vertices at those addresses now.
 */
#define VAR_MOVED 64
static struct { volatile vm_address_t start, end; } var_moved[VAR_MOVED];
static volatile unsigned var_moved_count;
static unsigned var_draws;	/* draws so far */
static struct var_vao *var_vao(void);
static void var_point(struct var_mirror *m);

static void var_context(void *ctx)
{
	if (var.ctx == ctx)
		return;
	if (var.ctx)
		rdn_log("vertex array range: context %p after %p, mirrors forgotten", ctx, var.ctx);
	/* The buffer objects are the other context's: nothing to delete here. */
	memset(&var, 0, sizeof(var));
	var_dirty_count = 0;
	var.ctx = ctx;
}

/*
 * The malloc block [ptr, ptr + len) lies in. A large block is page aligned
 * in a region of its own, or one the kernel has joined with its
 * neighbours': walk the blocks from the region's start, or, when that does
 * not start with one, the pages back from the pointer.
 */
static int var_block(const char *ptr, size_t len, const char **base, size_t *size)
{
	vm_address_t addr = (vm_address_t)ptr, a;
	vm_size_t region = 0;
	struct vm_region_basic_info info;
	mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT;
	mach_port_t object = MACH_PORT_NULL;
	/*
	 * Regions no block was found in: small blocks live there. Until
	 * malloc moves a large block: what a region is changes then.
	 */
	static struct { vm_address_t start, end; } none[32];
	static unsigned none_next, none_moved;
	size_t s;
	unsigned n;

	if (none_moved != var_moved_count) {
		none_moved = var_moved_count;
		memset(none, 0, sizeof(none));
	}
	for (n = 0; n < 32; n++)
		if ((vm_address_t)ptr >= none[n].start && (vm_address_t)ptr < none[n].end)
			return 0;
	if (vm_region(mach_task_self(), &addr, &region, VM_REGION_BASIC_INFO,
		      (vm_region_info_t)&info, &count, &object) ||
	    addr > (vm_address_t)ptr)
		return 0;
	for (a = addr, n = 0; n < 4096 && a <= (vm_address_t)ptr; n++) {
		s = malloc_size((void *)a);
		if (!s)
			break;
		if ((vm_address_t)ptr + len <= a + s)
			goto found;
		a += (s + 4095) & ~(size_t)4095;
	}
	for (a = (vm_address_t)ptr & ~(vm_address_t)4095, n = 0;
	     n < 16384 && a >= addr; n++, a -= 4096) {
		s = malloc_size((void *)a);
		if (s && (vm_address_t)ptr + len <= a + s)
			goto found;
		if (s || a < 4096)
			break;
	}
	none[none_next].start = addr;
	none[none_next++ % 32].end = addr + region;
	none_next %= 32;
	return 0;
found:
	if (s > (64u << 20))
		return 0;
	*base = (const char *)a;
	*size = s;
	return 1;
}

static void t_buffer_data(GLIContext ctx, GLenum target, GLsizeiptrARB size,
			  const GLvoid *data, GLenum usage);
static void t_buffer_sub_data(GLIContext ctx, GLenum target, GLintptrARB offset,
			      GLsizeiptrARB size, const GLvoid *data);

/* The mirror that has all of [ptr, ptr + len), the newest first. */
static struct var_mirror *var_find(const char *ptr, size_t len)
{
	static struct var_mirror *last;
	unsigned i;

	/* Most pointers follow one into the same mirror. */
	if (var.count && last == var.mirrors[var.count - 1] &&
	    ptr >= last->base && ptr + len <= last->base + last->size)
		return last;
	for (i = var.count; i-- > 0; ) {
		struct var_mirror *m = var.mirrors[i];

		if (ptr >= m->base && ptr + len <= m->base + m->size) {
			/* Kept at the end, where the search starts. */
			var.mirrors[i] = var.mirrors[var.count - 1];
			var.mirrors[var.count - 1] = last = m;
			return m;
		}
	}
	return NULL;
}

/*
 * Without a copy. A block of the program's memory that is page aligned (a
 * large malloc block is) goes behind the GART as it is, and the GPU reads
 * what the program wrote: no copy at a flush, and nothing a flush is
 * needed for. What that asks for:
 * - The mirror is of physical pages, not of addresses. When the program
 *   frees the block, the next thing malloc puts at that address is other
 *   memory, so the zone's free and realloc are watched and such a mirror
 *   is let go before anything is looked up again. Its pages stay wired,
 *   with what the program last wrote, until the GPU has drawn from them
 *   (the winsys waits before it unbinds).
 * - The GPU reads when it draws, not when the program says draw. A program
 *   that overwrites memory it has drawn from waits for a fence first, so
 *   glFinishFenceAPPLE is a real wait here (glFinish, when anything was
 *   drawn since the fence was set). glTestFenceAPPLE stays true: Call of
 *   Duty 2 uses it only to pace its frames.
 * Open: nothing bounds how far the GPU is behind, and the game takes a
 * fence two frames old for finished without asking.
 */
#define VAR_USERS 1024
#define VAR_USER_LEAST 65536
static struct {
	volatile vm_address_t base;
	volatile int freed;	/* the block was freed */
	volatile int taken;	/* a block was handed out at that address again */
} var_user[VAR_USERS];
static volatile int var_user_freed, var_user_taken;
static malloc_zone_t *var_zone;
static void (*var_zone_free)(malloc_zone_t *zone, void *ptr);
static void *(*var_zone_realloc)(malloc_zone_t *zone, void *ptr, size_t size);
static void *(*var_zone_malloc)(malloc_zone_t *zone, size_t size);
static void *(*var_zone_calloc)(malloc_zone_t *zone, size_t count, size_t size);
static void *(*var_zone_valloc)(malloc_zone_t *zone, size_t size);

/* Any thread, inside malloc: a page-aligned block comes or goes. */
static void var_moving(void *ptr, size_t size)
{
	unsigned i;

	/* One small block in 256 starts a page too; a large one is 15 KB up. */
	if (size < 15 * 1024)
		return;
	i = __sync_fetch_and_add(&var_moved_count, 1) % VAR_MOVED;
	var_moved[i].start = (vm_address_t)ptr;
	var_moved[i].end = (vm_address_t)ptr + size;
}

/* Any thread, inside malloc: only looks and sets flags. */
static void var_freeing(void *ptr)
{
	unsigned i;

	if (!ptr || ((vm_address_t)ptr & 4095))
		return;
	var_moving(ptr, var_zone->size(var_zone, ptr));
	for (i = 0; i < VAR_USERS; i++)
		if (var_user[i].base == (vm_address_t)ptr) {
			var_user[i].freed = 1;
			var_user_freed = 1;
		}
}

static void *var_taking(void *ptr, size_t size)
{
	unsigned i;

	if (!ptr || ((vm_address_t)ptr & 4095))
		return ptr;
	var_moving(ptr, size);
	if (size < VAR_USER_LEAST)
		return ptr;
	for (i = 0; i < VAR_USERS; i++)
		if (var_user[i].base == (vm_address_t)ptr) {
			var_user[i].taken = 1;
			var_user_taken = 1;
		}
	return ptr;
}

static void var_hook_free(malloc_zone_t *zone, void *ptr)
{
	var_freeing(ptr);
	var_zone_free(zone, ptr);
}

static void *var_hook_realloc(malloc_zone_t *zone, void *ptr, size_t size)
{
	var_freeing(ptr);
	return var_taking(var_zone_realloc(zone, ptr, size), size);
}

static void *var_hook_malloc(malloc_zone_t *zone, size_t size)
{
	return var_taking(var_zone_malloc(zone, size), size);
}

static void *var_hook_calloc(malloc_zone_t *zone, size_t count, size_t size)
{
	return var_taking(var_zone_calloc(zone, count, size), count * size);
}

static void *var_hook_valloc(malloc_zone_t *zone, size_t size)
{
	return var_taking(var_zone_valloc(zone, size), size);
}

/* Watch the default malloc zone, from the first mirror on. */
static void var_watch(void)
{
	malloc_zone_t *zone = malloc_default_zone();

	if (var_zone || !zone)
		return;
	var_zone_free = zone->free;
	var_zone_realloc = zone->realloc;
	var_zone_malloc = zone->malloc;
	var_zone_calloc = zone->calloc;
	var_zone_valloc = zone->valloc;
	var_zone = zone;
	zone->free = var_hook_free;
	zone->realloc = var_hook_realloc;
	zone->malloc = var_hook_malloc;
	zone->calloc = var_hook_calloc;
	zone->valloc = var_hook_valloc;
	rdn_log("vertex array range: watching what malloc zone %p frees and hands out",
		(void *)zone);
}

/*
 * Mirrors of memory in a block malloc has moved since the last look are
 * of memory that changes (var_drawing).
 */
static void var_moved_look(void)
{
	static unsigned seen;
	unsigned count = var_moved_count, k, i;

	if (count - seen > VAR_MOVED)
		seen = count - VAR_MOVED;
	for (k = seen; k != count; k++) {
		vm_address_t start = var_moved[k % VAR_MOVED].start;
		vm_address_t end = var_moved[k % VAR_MOVED].end;

		for (i = 0; i < var.count; i++) {
			struct var_mirror *m = var.mirrors[i];

			if ((vm_address_t)m->base < end && (vm_address_t)m->base + m->size > start)
				m->changed = 1;
		}
	}
	seen = count;
}

/* Let a mirror go; it is no longer in the list. */
static void var_drop(struct var_mirror *m)
{
	m_delete_buffers(1, &m->buffer);
	var.bytes -= m->size;
	if (m->user) {
		var_user[m->user - 1].base = 0;
		var_user[m->user - 1].freed = 0;
		var_user[m->user - 1].taken = 0;
	}
	free(m->shadow);
	m->shadow = NULL;
	if (m->refs)
		m->gone = 1;
	else
		free(m);
}

static void var_unref(struct var_mirror *m)
{
	if (!--m->refs && m->gone)
		free(m);
}

/*
 * The program freed memory the GPU was reading. The mirror stays, with
 * its buffer object, and is put over the memory anew when that address is
 * used again (var_revive): malloc gives the same address back, and a
 * program that sees the pointer it set last does not set it again (Call of
 * Duty 2 frees and takes its 2 MB of computed vertices 8 times a second),
 * so its vertex array objects must find the new memory behind the buffer
 * object they already have. A new buffer object drew the old memory:
 * flickering models.
 */
static void var_reap(void)
{
	unsigned i;

	var_user_freed = 0;
	for (i = 0; i < var.count; i++) {
		struct var_mirror *m = var.mirrors[i];

		if (m->user && var_user[m->user - 1].freed) {
			var_user[m->user - 1].freed = 0;
			var.freed++;
			if (m->stale)
				continue;
			m->stale = 1;
			/*
			 * The buffer object lets go of the freed pages (once
			 * the GPU has drawn from them): the game's vertex
			 * array objects keep the object itself for good, and
			 * an address malloc does not come back to would keep
			 * 2 MB wired each.
			 */
			m_bind_buffer(0x8892, m->buffer);
			m_buffer_data(0x8892, 4096, NULL, 0x88E8);
			m_bind_buffer(0x8892, 0);
		}
	}
}

static int var_block(const char *ptr, size_t len, const char **base, size_t *size);

/* Over the memory again, if the same block is there again. 0: it is not. */
static int var_revive(struct var_mirror *m)
{
	const char *base;
	size_t size;
	unsigned i;

	if (!var_block(m->base, m->size, &base, &size) || base != m->base || size != m->size)
		return 0;
	for (i = 0; i < 8 && m_get_error(); i++)
		;
	m_bind_buffer(0x9160, m->buffer);
	m_buffer_data(0x9160, (GLsizeiptrARB)m->size, m->base, 0x88E8);
	m_bind_buffer(0x9160, 0);
	if (m_get_error())
		return 0;
	m->stale = 0;
	var.revived++;
	return 1;
}

/*
 * Memory was handed out where a mirror had been freed. The program may
 * draw from it through a vertex array object it made for that address
 * before, with no flush and no pointer set, so the mirror goes over the
 * new memory now, before the next draw. Where the block is not there (or
 * no longer), the mirror stays as it is until the address is used.
 */
static void var_retake(void)
{
	unsigned i;

	var_user_taken = 0;
	if (var_user_freed)
		var_reap();
	for (i = 0; i < var.count; i++) {
		struct var_mirror *m = var.mirrors[i];

		if (m->user && var_user[m->user - 1].taken) {
			var_user[m->user - 1].taken = 0;
			if (m->stale)
				var_revive(m);
		}
	}
}

/* Take a mirror out of the list and let it go. */
static void var_remove(struct var_mirror *m)
{
	unsigned i;

	for (i = 0; i < var.count; i++)
		if (var.mirrors[i] == m) {
			var.mirrors[i] = var.mirrors[--var.count];
			var_drop(m);
			return;
		}
}

/*
 * A buffer object over the block itself, if this program is to have such
 * and the block allows it. 0 if not; the caller makes a copy then.
 */
static GLuint var_pin(const char *base, size_t size, unsigned short *user)
{
	static int have = -1;
	GLuint buffer = 0;
	unsigned slot, i;

	if (var_on() != 2 || size < VAR_USER_LEAST ||
	    ((vm_address_t)base & 4095) || (size & 4095))
		return 0;
	if (have < 0) {
		const char *list = (const char *)m_get_string(0x1F03);

		have = list && strstr(list, "GL_AMD_pinned_memory") && m_get_error && m_finish;
		rdn_log("vertex array range: the GPU %s read the program's memory",
			have ? "can" : "cannot");
	}
	if (!have)
		return 0;
	for (slot = 0; slot < VAR_USERS && var_user[slot].base; slot++)
		;
	if (slot == VAR_USERS)
		return 0;
	if (malloc_zone_from_ptr(base) != var_zone)
		return 0;
	for (i = 0; i < 8 && m_get_error(); i++)
		;
	m_gen_buffers(1, &buffer);
	m_bind_buffer(0x9160, buffer);	/* GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD */
	m_buffer_data(0x9160, (GLsizeiptrARB)size, base, 0x88E8);
	m_bind_buffer(0x9160, 0);
	if (m_get_error()) {
		/* No room behind the GART, most likely. */
		m_delete_buffers(1, &buffer);
		return 0;
	}
	var_user[slot].freed = 0;
	var_user[slot].taken = 0;
	var_user[slot].base = (vm_address_t)base;
	*user = slot + 1;
	return buffer;
}

/* Fences, when the GPU reads the program's memory. */
static unsigned var_fence_seq, var_fence_done, var_fence_at[4096];
static unsigned long var_fence_waits;

static void var_fence_set(GLuint fence)
{
	var_fence_at[fence & 4095] = ++var_fence_seq;
}

static void var_fence_finish(GLuint fence)
{
	if (var_on() != 2 || var_fence_at[fence & 4095] <= var_fence_done)
		return;
	m_finish();
	var_fence_done = var_fence_seq;
	var_fence_waits++;
	if (!(var_fence_waits & (var_fence_waits - 1)))
		rdn_log("vertex array range: %lu fences waited for so far", var_fence_waits);
}

/* Mirrors nothing points into and nothing has used for a while go. */
static void var_sweep(void)
{
	static unsigned swept;
	unsigned i, n = 0;

	if (var_swaps - swept < 256)
		return;
	swept = var_swaps;
	for (i = 0; i < var.count; i++) {
		struct var_mirror *m = var.mirrors[i];

		if (!m->refs && var_swaps - m->used > 120) {
			var_drop(m);
			continue;
		}
		var.mirrors[n++] = m;
	}
	if (n != var.count)
		rdn_log("vertex array range: %u mirrors let go, %u left, %lu KB",
			var.count - n, n, var.bytes >> 10);
	var.count = n;
}

static struct var_mirror *var_create(void *ctx, const char *ptr, size_t len)
{
	struct var_mirror *m;
	const char *base = ptr;
	size_t size = len;
	int block;

	var_settle(ctx);
	var_sweep();
	var_watch();
	if (var.count == var.room) {
		unsigned room = var.room ? var.room * 2 : 64;
		struct var_mirror **more = realloc(var.mirrors, room * sizeof(*more));

		if (!more)
			return NULL;
		var.mirrors = more;
		var.room = room;
	}
	m = calloc(1, sizeof(*m));
	if (!m)
		return NULL;
	block = var_block(ptr, len, &base, &size);
	m->base = base;
	m->size = size;
	m->used = var_swaps;
	if (!block || !(m->buffer = var_pin(base, size, &m->user))) {
		m->shadow = malloc(size);
		if (m->shadow)
			memcpy(m->shadow, base, size);
		m_gen_buffers(1, &m->buffer);
		m_bind_buffer(0x8892, m->buffer);
		t_buffer_data(ctx, 0x8892, (GLsizeiptrARB)size, base, 0x88E8);
		m_bind_buffer(0x8892, 0);
		var.copied += size;
	}
	var.mirrors[var.count++] = m;
	var.bytes += size;
	if (__builtin_expect(rdn_logging, 0))
		rdn_log("vertex array range: mirror %u of %p, %lu bytes (%s%s; asked %p, %lu): %u mirrors, %lu KB, %lu KB copied, %lu freed under us",
			m->buffer, (const void *)base, (unsigned long)size,
			block ? "the malloc block" : "the range",
			m->user ? ", not copied" : "",
			(const void *)ptr, (unsigned long)len, var.count, var.bytes >> 10,
			var.copied >> 10, var.freed);
	if (__builtin_expect(rdn_logging, 0) && var.revived && !(var.revived & (var.revived - 1)))
		rdn_log("vertex array range: %lu mirrors put over new memory so far", var.revived);
	return m;
}

/*
 * Bring the mirror's buffer object up to date in [from, to). A program
 * flushes far more than it changed (Call of Duty 2: the whole range again
 * for every new vertex array object over the same buffer), so only the
 * pieces that differ from the shadow are sent.
 */
#define VAR_PIECE 16384
static void var_update(void *ctx, struct var_mirror *m, const char *from, const char *to)
{
	const char *start = NULL, *p, *end;
	int bound = 0;

	if (!m->shadow) {
		m_bind_buffer(0x8892, m->buffer);
		t_buffer_sub_data(ctx, 0x8892, from - m->base, to - from, from);
		m_bind_buffer(0x8892, 0);
		var.copied += to - from;
		return;
	}
	/*
	 * Memory that is different every time it is flushed (vertices the
	 * program computes each frame) only pays for the shadow.
	 */
	m->seen += to - from;
	if (m->seen > (1u << 20)) {
		if (m->sent > m->seen / 4 * 3) {
			m->changed = 1;
			free(m->shadow);
			m->shadow = NULL;
			var_update(ctx, m, from, to);
			return;
		}
		m->seen = m->sent = 0;
	}
	for (p = from; p <= to; p = end) {
		int differs;

		end = to - p < VAR_PIECE ? to : p + VAR_PIECE;
		differs = p < to && memcmp(p, m->shadow + (p - m->base), end - p) != 0;
		if (differs && !start)
			start = p;
		if (start && (!differs || end == to)) {
			const char *stop = differs ? end : p;

			if (!bound++)
				m_bind_buffer(0x8892, m->buffer);
			t_buffer_sub_data(ctx, 0x8892, start - m->base, stop - start, start);
			memcpy(m->shadow + (start - m->base), start, stop - start);
			var.copied += stop - start;
			m->sent += stop - start;
			m->changed = 1;
			start = NULL;
		}
		if (p == to)
			break;
	}
	if (bound)
		m_bind_buffer(0x8892, 0);
}

/*
 * Flushes come in small pieces (Call of Duty 2: a call for every KB it
 * changed), so a mirror is brought up to date when something is about to
 * read it, with the pieces that touch joined.
 */
static void var_settle(void *ctx)
{
	unsigned i;

	for (i = 0; i < var_dirty_count; i++) {
		struct var_mirror *m = var_dirty[i];

		var_update(ctx, m, m->dirty, m->dirty_end);
		m->dirty = m->dirty_end = NULL;
	}
	var_dirty_count = 0;
}

static void var_mark(void *ctx, struct var_mirror *m, const char *from, const char *to)
{
	if (m->user)
		return;
	if (m->dirty && from <= m->dirty_end && to >= m->dirty) {
		if (from < m->dirty)
			m->dirty = from;
		if (to > m->dirty_end)
			m->dirty_end = to;
		return;
	}
	if (m->dirty || var_dirty_count == VAR_DIRTY)
		var_settle(ctx);
	m->dirty = from;
	m->dirty_end = to;
	var_dirty[var_dirty_count++] = m;
}

static void var_range(void *ctx, GLsizei length, const GLvoid *pointer)
{
	var_context(ctx);
	var_settle(ctx);
	if (__builtin_expect(var_user_freed, 0))
		var_reap();
	var.range = length > 0 ? pointer : NULL;
	var.range_size = length > 0 ? (size_t)length : 0;
}

static void var_flush(void *ctx, GLsizei length, const GLvoid *pointer)
{
	const char *ptr = pointer;
	size_t len = (size_t)length;
	struct var_mirror *whole = NULL;
	struct var_vao *v;
	unsigned i;

	if (length <= 0 || !pointer)
		return;
	var_context(ctx);
	if (__builtin_expect(var_user_freed, 0))
		var_reap();
	for (i = 0; i < var.count; i++) {
		struct var_mirror *m = var.mirrors[i];
		const char *from = ptr > m->base ? ptr : m->base;
		const char *to = ptr + len < m->base + m->size ? ptr + len : m->base + m->size;

		if (from >= to)
			continue;
		if (__builtin_expect(m->stale, 0) && !var_revive(m)) {
			/* Something else is at that address now. */
			var_remove(m);
			i--;
			continue;
		}
		if (from == ptr && to == ptr + len)
			whole = m;
		m->used = var_swaps;
		var_mark(ctx, m, from, to);
	}
	if (!whole)
		whole = var_create(ctx, ptr, len);
	/* For the draws that come without a flush: var_drawing(). */
	if (whole && !whole->user && (v = var_vao()) != NULL) {
		var_point(whole);
		v->in = whole;
		v->ptr = ptr;
		v->len = len;
		v->at = var_draws;
	}
}

/*
 * A draw is coming. A program is to flush what it changed, and Call of
 * Duty 2 does when it makes a vertex array object for a range; when it
 * draws from that range again through the same object, with other
 * vertices written there, it does not, since the GPU read its memory on
 * the Macs it ran on. So the range the bound object was last flushed over
 * is looked at again before every draw without a flush of its own, if its
 * mirror is of memory that has ever been seen to change (the world's 20 MB
 * are not compared for every draw). Without this the menu flickered after
 * a map: copies of the map's vertices at the addresses the menu draws
 * from.
 */
static void var_drawing(void *ctx)
{
	struct var_vao *v = var.ctx == ctx && var.vao < var.vao_room ? &var.vaos[var.vao] : NULL;
	static unsigned moved;
	unsigned i;

	if (__builtin_expect(moved != var_moved_count, 0)) {
		moved = var_moved_count;
		var_moved_look();
	}
	if (v && v->in && v->at != var_draws) {
		for (i = 0; i < VAR_VAO_MIRRORS && v->mirror[i] != v->in; i++)
			;
		if (i < VAR_VAO_MIRRORS && v->in->changed && !v->in->gone && !v->in->user &&
		    v->ptr >= v->in->base && v->ptr + v->len <= v->in->base + v->in->size) {
			v->in->used = var_swaps;
			var_mark(ctx, v->in, v->ptr, v->ptr + v->len);
		}
	}
	var_draws++;
}

/* The bound vertex array object's record; NULL if it cannot have one. */
static struct var_vao *var_vao(void)
{
	if (var.vao >= var.vao_room) {
		unsigned room = var.vao_room ? var.vao_room : 1024;
		struct var_vao *more;

		while (room <= var.vao)
			room *= 2;
		if (var.vao > (1u << 20) ||
		    !(more = realloc(var.vaos, room * sizeof(*more))))
			return NULL;
		memset(more + var.vao_room, 0, (room - var.vao_room) * sizeof(*more));
		var.vaos = more;
		var.vao_room = room;
	}
	return &var.vaos[var.vao];
}

/* The bound vertex array object points into the mirror from now on. */
static void var_point(struct var_mirror *m)
{
	struct var_vao *v = var_vao();
	unsigned i;

	m->used = var_swaps;
	/* A name beyond reason: the mirror stays for good instead. */
	if (!v) {
		m->refs++;
		return;
	}
	for (i = 0; i < VAR_VAO_MIRRORS; i++)
		if (v->mirror[i] == m)
			return;
	i = v->next++ % VAR_VAO_MIRRORS;
	if (v->mirror[i])
		var_unref(v->mirror[i]);
	v->mirror[i] = m;
	m->refs++;
}

/*
 * An array pointer: if it is in a mirror, or in the range named last (of
 * which a mirror is made then), bind the mirror's buffer object and make
 * the pointer an offset. The caller sets the array and unbinds.
 */
static int var_pointer(void *ctx, const GLvoid **pointer)
{
	const char *p = *pointer;
	struct var_mirror *m;
	GLuint *bound = map_bound(ctx, 0x8892);

	var_context(ctx);
	if (var_dirty_count)
		var_settle(ctx);
	if (__builtin_expect(var_user_freed, 0))
		var_reap();
	if (!p || (bound && *bound))
		return 0;
	m = var_find(p, 1);
	if (m && __builtin_expect(m->stale, 0) && !var_revive(m)) {
		var_remove(m);
		m = var_find(p, 1);
		if (m && m->stale)
			m = NULL;
	}
	if (!m && var.range && p >= var.range && p < var.range + var.range_size)
		m = var_create(ctx, var.range, var.range_size);
	if (!m)
		return 0;
	var_point(m);
	m_bind_buffer(0x8892, m->buffer);
	*pointer = (const GLvoid *)(p - m->base);
	return 1;
}

static void var_bind(void *ctx, GLuint id)
{
	var_context(ctx);
	if (var_dirty_count)
		var_settle(ctx);
	var.vao = id;
}

static void var_delete(void *ctx, GLsizei n, const GLuint *ids)
{
	GLsizei k;
	unsigned i;

	var_context(ctx);
	for (k = 0; k < n; k++) {
		if (ids[k] < var.vao_room) {
			struct var_vao *v = &var.vaos[ids[k]];

			for (i = 0; i < VAR_VAO_MIRRORS; i++)
				if (v->mirror[i])
					var_unref(v->mirror[i]);
			memset(v, 0, sizeof(*v));
		}
		if (ids[k] == var.vao)
			var.vao = 0;
	}
}

static void var_fences(GLsizei n, GLuint *fences)
{
	static GLuint last;
	GLsizei i;

	for (i = 0; i < n; i++)
		fences[i] = ++last;
}
"""

# The entries of the above that Mesa has no function for: ours when
# var_on(), the engine's otherwise.
VAR_OWN = {
    'vertex_array_range_EXT': '\tvar_range(ctx, count, pointer);',
    'flush_vertex_array_range_EXT': '\tvar_flush(ctx, count, pointer);',
    'vertex_array_parameteri_EXT': '\t(void)pname;\n\t(void)param;',
}

# GL_APPLE_fence's entries: ours when var_on() or apple_on() (APPLE_HELP).
FENCE_OWN = {
    'gen_fences_APPLE': '\tvar_fences(n, fences);',
    'delete_fences_APPLE': '\tfence_delete(n, fences);',
    'set_fence_APPLE': '\tfence_set(fence);',
    'is_fence_APPLE': '\treturn fence != 0;',
    'test_fence_APPLE': '\treturn fence_test(fence);',
    'finish_fence_APPLE': '\tfence_finish(fence);',
    'test_object_APPLE': '\treturn object_test(object, name);',
    'finish_object_APPLE': '\tobject_finish(object, name);',
}

# GL_APPLE_texture_range's and GL_APPLE_flush_render's: ours when
# apple_on().
APPLE_OWN = {
    'texture_range_APPLE': '\tapple_range(ctx, target, length, pointer);',
    'get_tex_parameter_pointerv_APPLE':
        '\tif (pname == 0x85B8)\t/* GL_TEXTURE_RANGE_POINTER_APPLE */\n'
        '\t\t*params = (GLvoid *)apple_range_pointer(ctx, target);',
    'flush_render_APPLE': '\t{ OSMesaContext cur = OSMesaGetCurrentContext(); if (cur) OSMesaFlushRender(cur, 0); else m_flush(); }',
    'finish_render_APPLE': '\t{ OSMesaContext cur = OSMesaGetCurrentContext(); if (cur) OSMesaFlushRender(cur, 1); else m_finish(); }',
}

# Array pointers that may point into a mirror.
VAR_POINTERS = ('vertex_pointer', 'normal_pointer', 'color_pointer',
                'tex_coord_pointer', 'vertex_attrib_pointer_ARB')

# glMapBuffer(GL_WRITE_ONLY) without waiting for the GPU. Mesa waits until
# the GPU is done with everything that uses the buffer, as OpenGL says, and
# r600 flushes first. World of Warcraft maps its vertex buffers that way
# many times a frame, each time to add to them (82 % of its main thread's
# time was that wait); on Leopard it asks for no wait with
# GL_APPLE_flush_buffer_range, which Tiger's OpenGL does not have. A
# program's choice, since OpenGL gives it no way to say:
#   RDN_MAPBUFFER=unsync   the buffer as it is, no wait: right when the
#                          program writes where no drawing it has asked
#                          for reads
#   RDN_MAPBUFFER=discard  new storage, no wait: right when the program
#                          writes all it will draw from again
#   RDN_MAPBUFFER=sync     Mesa's
# or a line "unsync Name" or "discard Name" in RDN_MAPBUFFER_LIST, the
# program's name as the system has it. Without either: sync.
MAP_BUFFER_HELP = """\
#define RDN_MAPBUFFER_LIST "/Library/Application Support/RadeonNI/mapbuffer"

static void *(*x_map_buffer_range)(GLenum target, GLintptr offset,
				   GLsizeiptr length, GLbitfield access);

static int map_mode_named(const char *mode)
{
	if (!strcmp(mode, "unsync"))
		return 1;
	if (!strcmp(mode, "discard"))
		return 2;
	return 0;
}

static int map_mode(void)
{
	static int mode = -1;
	const char *env, *name = getprogname();
	char line[256];
	FILE *f;

	if (mode >= 0)
		return mode;
	mode = 0;
	env = getenv("RDN_MAPBUFFER");
	if (env)
		mode = map_mode_named(env);
	else if (name && (f = fopen(RDN_MAPBUFFER_LIST, "r")) != NULL) {
		while (fgets(line, sizeof(line), f)) {
			char *who = strchr(line, ' ');

			line[strcspn(line, "\\r\\n")] = 0;
			if (!who)
				continue;
			*who++ = 0;
			if (!strcmp(who, name))
				mode = map_mode_named(line);
		}
		fclose(f);
	}
	if (mode)
		rdn_log("glMapBuffer(GL_WRITE_ONLY) does not wait (%s)",
			mode == 1 ? "unsync" : "discard");
	return mode;
}

/*
 * The sizes of buffers, for the range to map: asking Mesa
 * (glGetBufferParameteriv) makes the program's thread wait for glthread's
 * at every map. Kept from glBindBuffer and glBufferData, by engine context
 * and buffer name, for the array and element array targets; a buffer not
 * found here is asked for.
 */
#define MAP_CTXS 8
#define MAP_SIZES 4096
static struct { void *ctx; GLuint bound[2]; } map_ctxs[MAP_CTXS];
static struct {
	void *ctx;
	GLuint name;
	GLsizeiptrARB size;
	unsigned char nowait, noflush;	/* GL_APPLE_flush_buffer_range */
} map_sizes[MAP_SIZES];

static GLuint *map_bound(void *ctx, GLenum target)
{
	static unsigned next;
	unsigned i;

	if (target != 0x8892 && target != 0x8893)
		return NULL;
	for (i = 0; i < MAP_CTXS; i++)
		if (map_ctxs[i].ctx == ctx)
			return &map_ctxs[i].bound[target - 0x8892];
	i = next++ % MAP_CTXS;
	map_ctxs[i].ctx = ctx;
	map_ctxs[i].bound[0] = map_ctxs[i].bound[1] = 0;
	return &map_ctxs[i].bound[target - 0x8892];
}

static unsigned map_slot(void *ctx, GLuint name)
{
	unsigned i = (unsigned)(((unsigned long)ctx >> 4) * 31 + name * 2654435761u) % MAP_SIZES;
	unsigned n;

	for (n = 0; n < 16; n++, i = (i + 1) % MAP_SIZES)
		if (!map_sizes[i].ctx || (map_sizes[i].ctx == ctx && map_sizes[i].name == name))
			return i;
	return i;	/* crowded: the oldest of the run gives way */
}

static void map_note_size(void *ctx, GLenum target, GLsizeiptrARB size)
{
	GLuint *bound = map_bound(ctx, target);
	unsigned i;

	if (!bound || !*bound)
		return;
	i = map_slot(ctx, *bound);
	if (map_sizes[i].ctx != ctx || map_sizes[i].name != *bound)
		map_sizes[i].nowait = map_sizes[i].noflush = 0;
	map_sizes[i].ctx = ctx;
	map_sizes[i].name = *bound;
	map_sizes[i].size = size;
}

static GLsizeiptrARB map_known_size(void *ctx, GLenum target)
{
	GLuint *bound = map_bound(ctx, target);
	unsigned i;

	if (!bound || !*bound)
		return 0;
	i = map_slot(ctx, *bound);
	return map_sizes[i].ctx == ctx && map_sizes[i].name == *bound ? map_sizes[i].size : 0;
}

/*
 * GL_APPLE_flush_buffer_range. Tiger's OpenGL has neither of its two
 * functions; a program that knows the extension looks them up by name,
 * and the bundle answers that lookup itself (rdn_hook.c,
 * byname_function below). World of Warcraft does, and without the
 * extension it maps buffers the GPU is drawing from 1500 times a second,
 * each a wait in Mesa (30 frames a second at its login screen, 165 with).
 *
 * The program says for a buffer that mapping it need not wait for the GPU
 * (GL_BUFFER_SERIALIZED_MODIFY_APPLE false) and that it will name what it
 * wrote itself (GL_BUFFER_FLUSHING_UNMAP_APPLE false,
 * glFlushMappedBufferRangeAPPLE); in Mesa those are glMapBufferRange's
 * unsynchronized and explicit flush. The functions come without a
 * context: the one of the thread's last OpenGL call is meant.
 * RDN_NO_FLUSHRANGE=1 in a program's environment: no such extension.
 */
static int flushrange;
static unsigned long flushrange_calls[2];
static void (*x_flush_mapped_buffer_range)(GLenum target, GLintptr offset,
					   GLsizeiptr length);

static void flushrange_parameteri(GLenum target, GLenum pname, GLint param)
{
	GLuint *bound = map_bound(rdn_current_rend, target);
	unsigned i;

	if (!bound || !*bound)
		return;
	i = map_slot(rdn_current_rend, *bound);
	if (map_sizes[i].ctx != rdn_current_rend || map_sizes[i].name != *bound) {
		map_sizes[i].ctx = rdn_current_rend;
		map_sizes[i].name = *bound;
		map_sizes[i].size = 0;
		map_sizes[i].nowait = map_sizes[i].noflush = 0;
	}
	if (pname == 0x8A12)		/* GL_BUFFER_SERIALIZED_MODIFY_APPLE */
		map_sizes[i].nowait = !param;
	else if (pname == 0x8A13)	/* GL_BUFFER_FLUSHING_UNMAP_APPLE */
		map_sizes[i].noflush = !param;
	if (rdn_logging && flushrange_calls[0]++ < 16)
		rdn_log("glBufferParameteriAPPLE(0x%x, 0x%x, %d) for buffer %u",
			(unsigned)target, (unsigned)pname, (int)param, (unsigned)*bound);
}

static void flushrange_flush(GLenum target, GLintptr offset, GLsizeiptr size)
{
	GLuint *bound = map_bound(rdn_current_rend, target);
	unsigned i;

	if (!bound || !*bound || !x_flush_mapped_buffer_range)
		return;
	i = map_slot(rdn_current_rend, *bound);
	if (map_sizes[i].ctx == rdn_current_rend && map_sizes[i].name == *bound &&
	    map_sizes[i].noflush)
		x_flush_mapped_buffer_range(target, offset, size);
	if (rdn_logging && flushrange_calls[1]++ < 16)
		rdn_log("glFlushMappedBufferRangeAPPLE(0x%x, %ld, %ld) for buffer %u",
			(unsigned)target, (long)offset, (long)size, (unsigned)*bound);
}

/* What glMapBufferRange is to be asked for the bound buffer, or 0. */
static unsigned flushrange_bits(void *ctx, GLenum target)
{
	GLuint *bound = map_bound(ctx, target);
	unsigned i;

	if (!bound || !*bound)
		return 0;
	i = map_slot(ctx, *bound);
	if (map_sizes[i].ctx != ctx || map_sizes[i].name != *bound ||
	    (!map_sizes[i].nowait && !map_sizes[i].noflush))
		return 0;
	return 0x2 | (map_sizes[i].nowait ? 0x20 : 0) | (map_sizes[i].noflush ? 0x10 : 0);
}
"""

# Apple's extensions that are small enough to be the bundle's own, and the
# list of extensions as programs get it.
#
# For every program but the window server, which is right with the engine's
# entries and stays as it is. RDN_NO_APPLE=1 in a program's environment:
# the engine's entries for it too, and none of the names.
#
# GL_APPLE_fence, for programs without the vertex array range (there the
# fences stay what VAR_HELP says). A fence is a sync object of Mesa's:
# glTestFenceAPPLE asks it without waiting, glFinishFenceAPPLE waits. Both
# flush first: a program may poll a fence without ever flushing.
# glTestObjectAPPLE and glFinishObjectAPPLE ask whether the GPU is done
# with memory of the program's that an object refers to (a texture with
# client storage, a vertex array range); here such memory is copied when
# it is given, so the answer is always yes. Only GL_FENCE_APPLE is a wait.
#
# GL_APPLE_texture_range: hints about memory the texture's data is in.
# Nothing is done with them (texture data is copied); they are remembered
# so that a program can ask for them again. A texture that is deleted
# keeps its slot until another takes it: a new texture with the same name
# would be told the old range.
#
# GL_APPLE_client_storage, GL_APPLE_transform_hint and GL_APPLE_float_pixels
# add no function. The first two are values the entries in APPLE_ONLY take
# and drop; glGet* answers with what was set. The third's formats have the
# values of GL_ARB_texture_float's and GL_ARB_half_float_pixel's, which
# Mesa has; GL_COLOR_FLOAT_APPLE (is the drawable's colour floating point)
# is always false.
#
# GL_APPLE_flush_render: glFlush and glFinish without showing the picture,
# which is what Mesa's own two are.
#
# The names: EXT_DEFAULT for every program apple_on() is true for, and
# RDN_EXT_ADD="GL_one GL_two" in a program's environment adds any name,
# true or not, to see what the program does with it.
#
# Functions Tiger's framework lacks that a program looks up by name
# (rdn_hook.c): GL_APPLE_flush_buffer_range's two (MAP_BUFFER_HELP) and
# GL_EXT_gpu_program_parameters' two. Mesa names that extension and has
# the functions, but 10.4.11's libGL does not export them and its table
# has no entries for them; World of Warcraft asks for both by name and
# does without when it gets none. RDN_NO_PROGPARAMS=1: none from us
# either. With the log on, every OpenGL name a program looks up is logged,
# answered or not.
APPLE_HELP = """
static void *(*x_fence_sync)(GLenum condition, GLbitfield flags);
static GLenum (*x_client_wait_sync)(void *sync, GLbitfield flags, unsigned long long timeout);
static void (*x_delete_sync)(void *sync);

static int apple_on(void)
{
	static int on = -1;

	if (on < 0) {
		const char *name = getprogname();

		/* Not the window server, unless it does Core Image on the card. */
		on = name && (strcmp(name, "WindowServer") || rdn_ws_core_image()) &&
		     !getenv("RDN_NO_APPLE");
		if (on)
			rdn_log("GL_APPLE_fence, texture range and flush render: ours, for %s", name);
	}
	return on;
}

/* Fence names are var_fences()'s: one count for the whole program. */
static void *fence_syncs[4096];
static unsigned long fence_waits;

static void fence_drop(GLuint fence)
{
	void **sync = &fence_syncs[fence & 4095];

	if (*sync && x_delete_sync)
		x_delete_sync(*sync);
	*sync = NULL;
}

static void fence_set(GLuint fence)
{
	if (var_on()) {
		var_fence_set(fence);
		return;
	}
	fence_drop(fence);
	if (!x_fence_sync || !x_client_wait_sync)
		return;
	/*
	 * glFenceSync alone leaves out what glBegin and glEnd have gathered
	 * and not yet drawn: the fence would be reached before those are.
	 */
	m_flush();
	fence_syncs[fence & 4095] = x_fence_sync(0x9117, 0);	/* GL_SYNC_GPU_COMMANDS_COMPLETE */
}

static GLboolean fence_test(GLuint fence)
{
	void *sync = fence_syncs[fence & 4095];

	if (var_on() || !sync)
		return 1;
	/* GL_SYNC_FLUSH_COMMANDS_BIT, no wait; GL_TIMEOUT_EXPIRED */
	if (x_client_wait_sync(sync, 0x1, 0) == 0x911B)
		return 0;
	fence_drop(fence);
	return 1;
}

static void fence_finish(GLuint fence)
{
	void *sync = fence_syncs[fence & 4095];
	GLenum how = 0;
	unsigned i;

	if (var_on()) {
		var_fence_finish(fence);
		return;
	}
	if (!sync)
		return;
	/* A second at a time, and not for ever: a wait that fails ends it too. */
	for (i = 0; i < 10; i++)
		if ((how = x_client_wait_sync(sync, 0x1, 1000000000ull)) != 0x911B)
			break;
	fence_drop(fence);
	fence_waits++;
	if (!(fence_waits & (fence_waits - 1)))
		rdn_log("fences: %lu waited for so far, the last ended with 0x%x after %u seconds",
			fence_waits, (unsigned)how, i);
}

static void fence_delete(GLsizei n, const GLuint *fences)
{
	GLsizei i;

	for (i = 0; !var_on() && i < n; i++)
		fence_drop(fences[i]);
}

static GLboolean object_test(GLenum object, GLuint name)
{
	if (object == 0x8A0B && !var_on())	/* GL_FENCE_APPLE */
		return fence_test(name);
	return 1;
}

static void object_finish(GLenum object, GLuint name)
{
	if (object == 0x8A0B && !var_on())
		fence_finish(name);
	else if (var_on() == 2)
		m_finish();
}

/* What a context was last told of the values that are dropped. */
static struct apple_ctx {
	void *ctx;
	GLint client_storage, transform_hint;
} apple_ctxs[16];

static struct apple_ctx *apple_ctx(void *ctx)
{
	struct apple_ctx *c = &apple_ctxs[((unsigned long)ctx >> 4) & 15];

	if (c->ctx != ctx) {
		c->ctx = ctx;
		c->client_storage = 0;
		c->transform_hint = 0x1100;	/* GL_DONT_CARE */
	}
	return c;
}

static int apple_gets(GLenum pname)
{
	/* GL_UNPACK_CLIENT_STORAGE_APPLE, GL_TRANSFORM_HINT_APPLE, GL_COLOR_FLOAT_APPLE */
	return pname == 0x85B2 || pname == 0x85B1 || pname == 0x8A0F;
}

static GLint apple_get(void *ctx, GLenum pname)
{
	if (pname == 0x85B2)
		return apple_ctx(ctx)->client_storage;
	if (pname == 0x85B1)
		return apple_ctx(ctx)->transform_hint;
	return 0;
}

static struct apple_tex {
	void *ctx;
	GLenum target;
	GLuint name;
	const GLvoid *pointer;
	GLsizei length;
	GLint hint;
} apple_texs[256];

/* The record of the texture bound to a target, a new one if asked. */
static struct apple_tex *apple_tex(void *ctx, GLenum target, int make)
{
	/* 1D, 2D, 3D, rectangle, cube map: the binding to ask for. */
	GLenum binding = target == 0x0DE0 ? 0x8068 : target == 0x0DE1 ? 0x8069 :
			 target == 0x806F ? 0x806A : target == 0x84F5 ? 0x84F6 :
			 target == 0x8513 ? 0x8514 : 0;
	struct apple_tex *t;
	GLint name = 0;

	if (!binding || !m_get_integerv)
		return NULL;
	m_get_integerv(binding, &name);
	t = &apple_texs[((GLuint)name ^ target ^ (unsigned long)ctx >> 4) & 255];
	if (t->ctx == ctx && t->target == target && t->name == (GLuint)name)
		return t;
	if (!make)
		return NULL;
	t->ctx = ctx;
	t->target = target;
	t->name = (GLuint)name;
	t->pointer = NULL;
	t->length = 0;
	t->hint = 0x85BD;	/* GL_STORAGE_PRIVATE_APPLE */
	return t;
}

static void apple_range(void *ctx, GLenum target, GLsizei length, const GLvoid *pointer)
{
	struct apple_tex *t = apple_tex(ctx, target, 1);

	if (t) {
		t->pointer = pointer;
		t->length = length;
	}
}

static const GLvoid *apple_range_pointer(void *ctx, GLenum target)
{
	struct apple_tex *t = apple_tex(ctx, target, 0);

	return t ? t->pointer : NULL;
}

/* GL_TEXTURE_STORAGE_HINT_APPLE was set (the window server's are not kept). */
static void apple_hint(void *ctx, GLenum target, GLint hint)
{
	struct apple_tex *t = apple_on() ? apple_tex(ctx, target, 1) : NULL;

	if (t)
		t->hint = hint;
}

/* GL_TEXTURE_RANGE_LENGTH_APPLE (0x85B7) or GL_TEXTURE_STORAGE_HINT_APPLE. */
static GLint apple_tex_get(void *ctx, GLenum target, GLenum pname)
{
	struct apple_tex *t = apple_tex(ctx, target, 0);

	if (pname == 0x85B7)
		return t ? t->length : 0;
	return t ? t->hint : 0x85BD;
}

/* Looked up by name (rdn_hook.c). They come without a context, like
 * GL_APPLE_flush_buffer_range's: the thread's last is meant. */
static int byname, progparams;
static unsigned long progparams_calls[2];

static void progparams_env(GLenum target, GLuint index, GLsizei count, const GLfloat *params)
{
	if (rdn_logging && progparams_calls[0]++ < 4)
		rdn_log("glProgramEnvParameters4fvEXT(0x%x, %u, %d)", (unsigned)target,
			(unsigned)index, (int)count);
	m_program_env_parameters4fv_EXT(target, index, count, params);
}

static void progparams_local(GLenum target, GLuint index, GLsizei count, const GLfloat *params)
{
	if (rdn_logging && progparams_calls[1]++ < 4)
		rdn_log("glProgramLocalParameters4fvEXT(0x%x, %u, %d)", (unsigned)target,
			(unsigned)index, (int)count);
	m_program_local_parameters4fv_EXT(target, index, count, params);
}

static void *byname_function(const char *name)
{
	void *own = NULL;

	if (name[0] != 'g' || name[1] != 'l')
		return NULL;
	if (flushrange && !strcmp(name, "glBufferParameteriAPPLE"))
		own = (void *)flushrange_parameteri;
	else if (flushrange && !strcmp(name, "glFlushMappedBufferRangeAPPLE"))
		own = (void *)flushrange_flush;
	else if (progparams && m_program_env_parameters4fv_EXT &&
		 !strcmp(name, "glProgramEnvParameters4fvEXT"))
		own = (void *)progparams_env;
	else if (progparams && m_program_local_parameters4fv_EXT &&
		 !strcmp(name, "glProgramLocalParameters4fvEXT"))
		own = (void *)progparams_local;
	rdn_log("looked up by name: %s, %s", name, own ? "ours" : "not ours");
	return own;
}

/*
 * RDN_RENDERER="name" in a program's environment: what glGetString says
 * for GL_RENDERER instead of Mesa's name. Core Image sorts renderers by
 * the renderer ID or, failing that, by how this name starts ("ATI Radeon
 * X1", "ATI Radeon ", "NVIDIA GeForce ", "Intel ", ...); one it does not
 * know gets no buffer formats and a speed of 0, and it filters on the CPU.
 */
static const char *renderer_name(void)
{
	static const char *name;
	static int asked;

	if (!asked) {
		asked = 1;
		name = getenv("RDN_RENDERER");
		/* RDN_RENDERER= (empty): Mesa's own name. Else the default is the
		 * name Core Image knows (the user's decision, 2026-10-08). */
		if (!name && (!rdn_ws_no_core_image()))
			name = "ATI Radeon HD 7570";
		if (name && !*name)
			name = NULL;
		if (name)
			rdn_log("GL_RENDERER is \\"%s\\" for this program (RDN_RENDERER)", name);
	}
	return name;
}

/* Names for every program apple_on() is true for. */
static const char *const ext_default[] = {
@DEFAULTS@	NULL
};

static int ext_ours(void)
{
	static int ours = -1;

	if (ours < 0)
		ours = flushrange || var_on() || (apple_on() && ext_default[0]) ||
		       getenv("RDN_EXT_ADD") != NULL || rdn_ws_no_core_image() ||
		       rdn_ws_core_image();
	return ours;
}

/* A name out of the list, if it is there. */
static void ext_drop(char *list, const char *name)
{
	size_t len = strlen(name);
	char *at = list, *end;

	for (; *at; at = end) {
		while (*at == ' ')
			at++;
		for (end = at; *end && *end != ' '; end++)
			;
		if ((size_t)(end - at) == len && !strncmp(at, name, len)) {
			while (*end == ' ')
				end++;
			memmove(at, end, strlen(end) + 1);
			return;
		}
	}
}

/* One more name at the end of the list, unless it is there. */
static void ext_add(char *list, const char *name, size_t len)
{
	const char *at = list, *end;

	for (; *at; at = end) {
		while (*at == ' ')
			at++;
		for (end = at; *end && *end != ' '; end++)
			;
		if ((size_t)(end - at) == len && !strncmp(at, name, len))
			return;
	}
	if (*list)
		strcat(list, " ");
	strncat(list, name, len);
}

/* The list of extensions with ours at its end. */
static const GLubyte *ext_list(const GLubyte *mesa)
{
	static const char *const range[] = {
		"GL_APPLE_vertex_array_range", "GL_APPLE_fence",
		"GL_APPLE_vertex_array_object", "GL_APPLE_element_array",
	};
	static char *list;
	const char *add = getenv("RDN_EXT_ADD"), *end;
	size_t room;
	unsigned i;

	if (list || !mesa)
		return list ? (const GLubyte *)list : mesa;
	room = strlen((const char *)mesa) + 64 + 4 * 40 + (add ? strlen(add) + 2 : 0);
	for (i = 0; ext_default[i]; i++)
		room += strlen(ext_default[i]) + 1;
	list = malloc(room);
	if (!list)
		return mesa;
	strcpy(list, (const char *)mesa);
	if (flushrange)
		ext_add(list, "GL_APPLE_flush_buffer_range", 27);
	for (i = 0; var_on() && i < 4; i++)
		ext_add(list, range[i], strlen(range[i]));
	for (i = 0; apple_on() && ext_default[i]; i++)
		ext_add(list, ext_default[i], strlen(ext_default[i]));
	for (; add && *add; add = end) {
		while (*add == ' ' || *add == ',')
			add++;
		for (end = add; *end && *end != ' ' && *end != ','; end++)
			;
		if (end > add)
			ext_add(list, add, end - add);
	}
	/* Core Image on the card in the window server: what it requires. */
	if (rdn_ws_core_image()) {
		ext_add(list, "GL_APPLE_client_storage", 23);
		ext_add(list, "GL_APPLE_float_pixels", 21);
		rdn_log("the window server is told of GL_APPLE_client_storage and GL_APPLE_float_pixels (Core Image)");
	}
	/* Not for the window server (RadeonNIGLDriver.c, rdn_ws_no_core_image). */
	if (rdn_ws_no_core_image()) {
		ext_drop(list, "GL_ARB_fragment_program");
		rdn_log("the window server is not told of GL_ARB_fragment_program (no Core Image)");
	}
	rdn_log("extensions of ours named in the list:%s%s%s%s%s",
		flushrange ? " GL_APPLE_flush_buffer_range" : "",
		var_on() ? " GL_APPLE_vertex_array_range and what goes with it" : "",
		apple_on() && ext_default[0] ? " the bundle's own Apple names" : "",
		getenv("RDN_EXT_ADD") ? " and RDN_EXT_ADD's: " : "",
		getenv("RDN_EXT_ADD") ? getenv("RDN_EXT_ADD") : "");
	return (const GLubyte *)list;
}
"""

# The names in ext_default: what the bundle's own entries above make true.
# Tried through RDN_EXT_ADD on the G5 first (Quake 3, which asks for the
# transform hint, Call of Duty 2, Chess). Not GL_APPLE_client_storage and
# GL_APPLE_float_pixels: with them and a renderer name it knows, Core
# Image takes the card and then needs pbuffers, which we lack.
# GL_APPLE_vertex_array_object is Mesa's GL_ARB_vertex_array_object under
# Apple's names; one difference is left: Apple's lets a program bind a
# name it never generated, Mesa's does not.
EXT_DEFAULT = (
    'GL_APPLE_transform_hint',
    'GL_APPLE_fence',
    'GL_APPLE_vertex_array_object',
    'GL_APPLE_texture_range',
    'GL_APPLE_flush_render',
    'GL_APPLE_client_storage',
    'GL_APPLE_float_pixels',
)

MAP_BUFFER = """\tif (access == 0x88B9 && x_map_buffer_range && flushrange) {
\t\tunsigned bits = flushrange_bits(ctx, target);
\t\tGLint size = (GLint)map_known_size(ctx, target);

\t\tif (bits && size > 0)
\t\t\treturn x_map_buffer_range(target, 0, size, bits);
\t}
\tif (access == 0x88B9 && x_map_buffer_range && map_mode()) {
\t\tGLint size = (GLint)map_known_size(ctx, target);

\t\tif (size <= 0)
\t\t\tm_get_buffer_parameteriv(target, 0x8764, &size);	/* GL_BUFFER_SIZE */
\t\tif (size > 0)	/* write; unsynchronized or invalidate buffer */
\t\t\treturn x_map_buffer_range(target, 0, size,
\t\t\t\t\t\t  map_mode() == 1 ? 0x2 | 0x20 : 0x2 | 0x8);
\t}"""

# With the log on, the text of every ARB program and what Mesa said to it.
# glDrawRangeElements with a range wider than the draw has indices. The
# range is a promise, not the truth: Call of Duty 2 gives the whole vertex
# buffer for every draw, 20 MB of it. It draws from its own memory (Apple's
# vertex array range, no buffer objects), so Mesa copies the range named
# for every draw and runs out of video memory. Without the range Mesa
# reads the indices and copies what they use; with buffer objects it reads
# nothing either way. RDN_GLD_RANGE=1 passes the range on as given.
DRAW_RANGE = """\tif (end - start >= (GLuint)count && !draw_range_as_given()) {
\t\tm_draw_elements(mode, count, type, indices);
\t\treturn;
\t}"""

DRAW_RANGE_HELP = """
static int draw_range_as_given(void)
{
	static int given = -1;

	if (given < 0)
		given = getenv("RDN_GLD_RANGE") != NULL;
	return given;
}
"""

PROGRAM_STRING = """\tchar *own = weight_as_attrib(target, string, &len);

\tif (own)
\t\tstring = own;
\tm_program_string_ARB(target, format, len, string);
\tif (__builtin_expect(rdn_logging, 0)) {
\t\tGLint at = -1;

\t\tm_get_integerv(0x864B, &at);	/* GL_PROGRAM_ERROR_POSITION_ARB */
\t\trdn_log("program: target 0x%x, %d bytes, error position %d%s%s\\n%.*s",
\t\t\t(unsigned)target, (int)len, (int)at, at >= 0 ? ": " : "",
\t\t\tat >= 0 ? (const char *)m_get_string(0x8874) : "",
\t\t\t(int)len, (const char *)string);
\t}
\tfree(own);"""

APPLE_ONLY = {
    'pixel_storei': ('pname', (0x85B2,), 'apple_ctx(ctx)->client_storage = param != 0;'),
    'pixel_storef': ('pname', (0x85B2,), 'apple_ctx(ctx)->client_storage = param != 0;'),
    'tex_parameteri': ('pname', (0x85BC,), 'apple_hint(ctx, target, (GLint)param);'),
    'tex_parameterf': ('pname', (0x85BC,), 'apple_hint(ctx, target, (GLint)param);'),
    'tex_parameteriv': ('pname', (0x85BC,), 'apple_hint(ctx, target, (GLint)params[0]);'),
    'tex_parameterfv': ('pname', (0x85BC,), 'apple_hint(ctx, target, (GLint)params[0]);'),
    'hint': ('target', (0x85B1,), 'apple_ctx(ctx)->transform_hint = (GLint)mode;'),
}

# glGet* of what the entries above took, and of GL_COLOR_FLOAT_APPLE: the
# cast that stores the answer.
APPLE_GETS = {
    'get_booleanv': '(GLboolean)(%s != 0)',
    'get_integerv': '%s',
    'get_floatv': '(GLfloat)%s',
    'get_doublev': '(GLdouble)%s',
}

# glGetProgramivARB for a vertex program of the counts only fragment
# programs have (ALU and texture instructions, texture indirections, used
# and most, native or not: 0x8805 to 0x8810). OpenGL says invalid enum, and
# Mesa says so and leaves the answer unset; Apple's renderers answer 0
# without an error, and Core Image asks for the three native limits of
# both kinds of program before it decides whether to use the card.
PROGRAM_GET = """\tif (__builtin_expect(target == 0x8620 && pname >= 0x8805 && pname <= 0x8810, 0) &&
\t    apple_on()) {
\t\t*params = 0;
\t\treturn;
\t}"""

# glGetTexParameter* of GL_APPLE_texture_range's two values.
APPLE_TEX_GETS = {
    'get_tex_parameteriv': '%s',
    'get_tex_parameterfv': '(GLfloat)%s',
}

# Entries that carry data for a buffer object. With glthread, more than
# rdn_async_limit bytes in one call make the program's thread wait for all
# the calls before it; in pieces they do not (rdn_glue.h). The buffer ends
# up the same: glBufferData without data makes the store, glBufferSubData
# fills it.
SPLIT = {
    'buffer_data': """\
	/* Not GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD: that data is not copied. */
	if (__builtin_expect(rdn_async_limit && data && size > rdn_async_limit, 0) &&
	    m_buffer_sub_data && target != 0x9160) {
		GLsizeiptrARB done, n;

		m_buffer_data(target, size, 0, usage);
		for (done = 0; done < size; done += n) {
			n = size - done < rdn_async_piece ? size - done : rdn_async_piece;
			m_buffer_sub_data(target, done, n, (const char *)data + done);
		}
		return;
	}""",
    'buffer_sub_data': """\
	if (__builtin_expect(rdn_async_limit && data && size > rdn_async_limit, 0)) {
		GLsizeiptrARB done, n;

		for (done = 0; done < size; done += n) {
			n = size - done < rdn_async_piece ? size - done : rdn_async_piece;
			m_buffer_sub_data(target, offset + done, n, (const char *)data + done);
		}
		return;
	}""",
}

# glFlush for a drawable whose picture is copied to memory: with glthread
# the copy would be made some time after the call has returned, so there
# it is glFinish (rdn_glue.h).
FLUSH = """\
	if (__builtin_expect(rdn_flush_waits, 0) && m_finish) {
		m_finish();
		return;
	}"""

# What goes before the wrappers of the entries the engine keeps. The
# engine's functions are remembered per engine context (the one the table
# belongs to: a CGL context is `rend` followed by `disp`, CGLContext.h),
# because nothing says that two contexts' tables hold the same ones; a call
# for a context no table was wrapped for gets the function saved last. The
# engine may put its own functions back at any time: every install saves
# what it finds, unless that is the wrapper itself.
KEPT = """\
/*
 * Counting the calls the engine keeps (only while the bundle logs). The
 * first KEPT_LOGGED calls of an entry are logged, all of them with the GL
 * trace on.
 */
#define KEPT_LOGGED 8
#define KEPT_TABLES 64

static unsigned long kept_calls[%(count)d];
static void *kept_last[%(count)d];
static struct kept_table {
	void *rend;
	unsigned wraps;
	void *real[%(count)d];
} kept_tables[KEPT_TABLES];
static struct kept_table *kept_recent;

static void *kept_real(void *rend, unsigned entry)
{
	struct kept_table *t = kept_recent;
	static unsigned noted;
	unsigned i;

	if (t && t->rend == rend && t->real[entry])
		return t->real[entry];
	for (i = 0; i < KEPT_TABLES; i++) {
		t = &kept_tables[i];
		if (t->rend == rend && t->real[entry]) {
			kept_recent = t;
			return t->real[entry];
		}
	}
	if (noted++ < 16)
		rdn_log("kept: %%s called for engine context %%p, which has no table of ours",
			lookups[entry].name, rend);
	return kept_last[entry];
}

static struct kept_table *kept_table(void *rend)
{
	static unsigned next;
	struct kept_table *t;
	unsigned i;

	for (i = 0; i < KEPT_TABLES; i++)
		if (kept_tables[i].rend == rend)
			return &kept_tables[i];
	/* None yet: the next in turn, whatever it held. */
	t = &kept_tables[next++ %% KEPT_TABLES];
	if (kept_recent == t)
		kept_recent = NULL;
	memset(t, 0, sizeof(*t));
	t->rend = rend;
	return t;
}
"""

# Entries whose x and y arguments are window coordinates.
WINDOW_XY = ('viewport', 'scissor', 'read_pixels', 'copy_pixels',
             'copy_tex_image1D', 'copy_tex_image2D', 'copy_tex_sub_image1D',
             'copy_tex_sub_image2D', 'copy_tex_sub_image3D')


def main():
    text = open(sys.argv[1]).read()
    body = text[text.index('__GLIFunctionDispatchRec'):]
    body = body[body.index('{') + 1:body.index('}')]
    entries = []
    for decl in body.split(';'):
        m = ENTRY.match(decl + ';')
        if not m:
            continue
        ret, name, params = m.group(1).strip(), m.group(2), (m.group(3) or '').strip()
        names = arg_names(params) if params else []
        if names is None:
            continue
        entries.append((ret, name, params, names))

    out = ['/* Generated by gen_dispatch.py from the SDK\'s gliDispatch.h. */',
           '#include <stddef.h>', '#include <stdio.h>', '#include <stdlib.h>',
           '#include <string.h>', '#include <unistd.h>', '#include <sys/time.h>', '#include <mach/mach.h>', '#include <malloc/malloc.h>',
           '#include <OpenGL/CGLContext.h>', 'typedef struct osmesa_context *OSMesaContext;', 'OSMesaContext OSMesaGetCurrentContext(void);', 'void OSMesaFlushRender(OSMesaContext ctx, unsigned char wait);',
           '#include "rdn_dispatch.h"', '']
    index = dict((e[1], i) for i, e in enumerate(entries))
    for name in list(SPLIT) + ['flush', 'finish']:
        if name not in [e[1] for e in entries]:
            sys.exit('no entry %s' % name)
    # All of Mesa's first: an entry may call more than its own.
    for ret, name, params, names in entries:
        out.append('static %s (*m_%s)(%s);' % (ret, name, params if params else 'void'))
    out.append('')
    out.append(WEIGHT)
    out.append(MAP_BUFFER_HELP.replace('static int flushrange;', 'static int flushrange;\nstatic int var_on(void);', 1))
    out.append(VAR_HELP)
    out.append(APPLE_HELP.replace('@DEFAULTS@', ''.join('\t"%s",\n' % n for n in EXT_DEFAULT)))
    out.append(DRAW_RANGE_HELP)
    bodies = dict(OWN)
    for more in (VAR_OWN, FENCE_OWN, APPLE_OWN):
        bodies.update(more)
    for name in list(bodies) + list(APPLE_ONLY) + list(APPLE_GETS) + list(APPLE_TEX_GETS):
        if name not in index:
            sys.exit('no entry %s' % name)
    for ret, name, params, names in entries:
        full = 'GLIContext ctx' + (', ' + params if params else '')
        call = 'm_%s(%s)' % (name, ', '.join(names))
        out.append('static %s t_%s(%s)' % (ret, name, full))
        out.append('{')
        out.append('\tRDN_ENTER(ctx);')
        if name in bodies:
            out.append('\tif (__builtin_expect(rdn_trace, 0) && rdn_trace_wanted("%s"))' % gl_name(name))
            out.append('\t\t%s;' % trace_call(name, params, names))
            out.append(bodies[name])
            out.append('}')
            out.append('')
            continue
        out.append('\tif (__builtin_expect(rdn_trace, 0) && rdn_trace_wanted("%s"))' % gl_name(name))
        out.append('\t\t%s;' % trace_call(name, params, names))
        if name in APPLE_ONLY:
            arg, values, note = APPLE_ONLY[name]
            if arg not in names:
                sys.exit('%s has no %s' % (name, arg))
            out.append('\tif (%s) {' % ' || '.join('%s == 0x%X' % (arg, v) for v in values))
            out.append('\t\t%s' % note)
            out.append('\t\treturn;')
            out.append('\t}')
        if name in APPLE_GETS:
            out.append('\tif (__builtin_expect(apple_gets(pname), 0) && apple_on()) {')
            out.append('\t\t*params = %s;' % (APPLE_GETS[name] % 'apple_get(ctx, pname)'))
            out.append('\t\treturn;')
            out.append('\t}')
        if name == 'get_programiv_ARB':
            out.append(PROGRAM_GET)
        if name in APPLE_TEX_GETS:
            out.append('\tif (__builtin_expect(pname == 0x85B7 || pname == 0x85BC, 0) && apple_on()) {')
            out.append('\t\t*params = %s;' % (APPLE_TEX_GETS[name] % 'apple_tex_get(ctx, target, pname)'))
            out.append('\t\treturn;')
            out.append('\t}')
        if name == 'map_buffer':
            out.append(MAP_BUFFER)
        if name in ('draw_range_elements', 'draw_elements', 'draw_arrays'):
            out.append('\tif (var_on())')
            out.append('\t\tvar_drawing(ctx);')
            out.append('\tif (__builtin_expect(var_dirty_count, 0))')
            out.append('\t\tvar_settle(ctx);')
            out.append('\tif (__builtin_expect(var_user_taken, 0))')
            out.append('\t\tvar_retake();')
        if name == 'draw_range_elements':
            out.append(DRAW_RANGE)
        if name in VAR_POINTERS:
            out.append('\tif (var_on() && var_pointer(ctx, &pointer)) {')
            out.append('\t\t%s;' % call)
            out.append('\t\tm_bind_buffer(0x8892, 0);')
            out.append('\t\treturn;')
            out.append('\t}')
        if name == 'bind_vertex_array_EXT':
            out.append('\tif (var_on())')
            out.append('\t\tvar_bind(ctx, id);')
        if name == 'delete_vertex_arrays_EXT':
            out.append('\tif (var_on())')
            out.append('\t\tvar_delete(ctx, n, ids);')
        if name == 'get_string':
            out.append('\tif (name == 0x1F03 && ext_ours())\t/* GL_EXTENSIONS */')
            out.append('\t\treturn ext_list(m_get_string(name));')
            out.append('\tif (name == 0x1F01 && renderer_name())\t/* GL_RENDERER */')
            out.append('\t\treturn (const GLubyte *)renderer_name();')
        if name == 'bind_buffer':
            out.append('\tif (map_mode() || flushrange || var_on()) {')
            out.append('\t\tGLuint *bound = map_bound(ctx, target);')
            out.append('')
            out.append('\t\tif (bound)')
            out.append('\t\t\t*bound = buffer;')
            out.append('\t}')
        if name == 'buffer_data':
            out.append('\tif (map_mode() || flushrange)')
            out.append('\t\tmap_note_size(ctx, target, size);')
        if name in CLIENT_STATE:
            out.append('\tif (array == 0x851D && var_on())\t/* GL_VERTEX_ARRAY_RANGE_APPLE */')
            out.append('\t\treturn;')
            out.append('\tif (array == 0x86AD) {')
            out.append('\t\t%s(1);' % CLIENT_STATE[name])
            out.append('\t\treturn;')
            out.append('\t}')
        if name in WINDOW_XY:
            if 'x' not in names or 'y' not in names:
                sys.exit('%s has no x and y' % name)
            out.append('\tx += rdn_origin_x;')
            out.append('\ty += rdn_origin_y;')
        if name in SPLIT:
            out.append(SPLIT[name])
        if name == 'flush':
            out.append(FLUSH)
        if name == 'program_string_ARB':
            out.append(PROGRAM_STRING)
            out.append('}')
            out.append('')
            continue
        out.append('\t%s%s;' % ('' if ret == 'void' else 'return ', call))
        if name in ('flush', 'finish'):
            out.append('\trdn_flush_surface(ctx);')
        if name in WATCHED:
            out.append('\tif (__builtin_expect(rdn_watch, 0))')
            out.append('\t\trdn_watch_%s(%s);' % (name, ', '.join(names)))
        out.append('}')
        out.append('')

    out.append('static const struct { const char *name; void **mesa; } lookups[] = {')
    for ret, name, params, names in entries:
        out.append('\t{ "%s", (void **)&m_%s },' % (gl_name(name), name))
    out.append('};')
    out.append('')
    # The engine's own, counted. swap_APPLE is the bundle's (below).
    out.append(KEPT % {'count': len(entries)})
    for ret, name, params, names in entries:
        if name == 'swap_APPLE':
            continue
        full = 'GLIContext ctx' + (', ' + params if params else '')
        out.append('static %s w_%s(%s)' % (ret, name, full))
        out.append('{')
        out.append('\t%s (*real)(%s) = kept_real(ctx, %d);' % (ret, full, index[name]))
        out.append('')
        out.append('\tif (kept_calls[%d]++ < KEPT_LOGGED ||' % index[name])
        out.append('\t    (rdn_trace && rdn_trace_wanted("%s")))' % gl_name(name))
        out.append('\t\t%s;' % trace_call(name, params, names, 'kept: ',
                                          ' engine context %p', ', (void *)ctx'))
        out.append('\tif (!real)')
        out.append('\t\treturn%s;' % ('' if ret == 'void' else ' 0'))
        out.append('\t%sreal(%s);' % ('' if ret == 'void' else 'return ',
                                     ', '.join(['ctx'] + names)))
        out.append('}')
        out.append('')
    out.append('unsigned rdn_dispatch_resolve(void *(*lookup)(const char *name),')
    out.append('\t\t\t      void (*missing)(const char *name))')
    out.append('{')
    out.append('\tunsigned i, found = 0;')
    out.append('')
    out.append('\tx_map_buffer_range = lookup("glMapBufferRange");')
    out.append('\tx_flush_mapped_buffer_range = lookup("glFlushMappedBufferRange");')
    out.append('\tx_fence_sync = lookup("glFenceSync");')
    out.append('\tx_client_wait_sync = lookup("glClientWaitSync");')
    out.append('\tx_delete_sync = lookup("glDeleteSync");')
    out.append('\tbyname = strcmp(getprogname(), "WindowServer") &&')
    out.append('\t\trdn_hook_function_lookup(byname_function);')
    out.append('\tflushrange = byname && x_map_buffer_range && x_flush_mapped_buffer_range &&')
    out.append('\t\t!getenv("RDN_NO_FLUSHRANGE");')
    out.append('\tprogparams = byname && !getenv("RDN_NO_PROGPARAMS");')
    out.append('\tfor (i = 0; i < sizeof(lookups) / sizeof(lookups[0]); i++) {')
    out.append('\t\t*lookups[i].mesa = lookup(lookups[i].name);')
    out.append('\t\tif (*lookups[i].mesa)')
    out.append('\t\t\tfound++;')
    out.append('\t\telse if (missing)')
    out.append('\t\t\tmissing(lookups[i].name);')
    out.append('\t}')
    out.append('\treturn found;')
    out.append('}')
    out.append('')
    # Presenting: the engine's own entry, unless the bundle shows the
    # context's picture itself (a window taken as a surface).
    out.append('static void (*engine_swap)(GLIContext ctx);')
    # RDN_FPS=1 in a program's environment: frames a second on standard
    # error, every five seconds.
    out.append('static void swap_count(void)')
    out.append('{')
    out.append('\tstatic int on = -1;')
    out.append('\tstatic unsigned frames;')
    out.append('\tstatic struct timeval since;')
    out.append('\tstruct timeval now;')
    out.append('\tdouble s;')
    out.append('')
    out.append('\tif (on < 0)')
    out.append('\t\ton = getenv("RDN_FPS") != NULL;')
    out.append('\tif (!on)')
    out.append('\t\treturn;')
    out.append('\tgettimeofday(&now, NULL);')
    out.append('\tif (!since.tv_sec)')
    out.append('\t\tsince = now;')
    out.append('\tframes++;')
    out.append('\ts = (now.tv_sec - since.tv_sec) + (now.tv_usec - since.tv_usec) / 1e6;')
    out.append('\tif (s >= 5) {')
    out.append('\t\tfprintf(stderr, "RadeonNI: %.1f frames a second\\n", frames / s);')
    out.append('\t\tframes = 0;')
    out.append('\t\tsince = now;')
    out.append('\t}')
    out.append('}')
    out.append('')
    out.append('static void rdn_swap_entry(GLIContext ctx)')
    out.append('{')
    out.append('\tswap_count();')
    out.append('\tvar_swaps++;')
    out.append('\tif (__builtin_expect(rdn_logging, 0))')
    out.append('\t\tkept_calls[%d]++;' % index['swap_APPLE'])
    out.append('\tif (!rdn_swap(ctx) && engine_swap)')
    out.append('\t\tengine_swap(ctx);')
    out.append('}')
    out.append('')
    out.append('#define FITS(field) (offsetof(GLIFunctionDispatch, field) / sizeof(void *) < entries)')
    out.append('')
    # Wrap what Mesa lacks, in a table install has just been through.
    out.append('#define KEEP(field, entry) \\')
    out.append('\tif (!m_##field && FITS(field) && disp->field && disp->field != w_##field && \\')
    out.append('\t    disp->field != t_##field && \\')
    out.append('\t    kept_wanted(#field, k++)) { \\')
    out.append('\t\tt->real[entry] = kept_last[entry] = (void *)disp->field; \\')
    out.append('\t\tdisp->field = w_##field; \\')
    out.append('\t\tn++; \\')
    out.append('\t}')
    out.append('')
    # Experiment: RDN_GLD_KEPT="first-last" wraps only those of the
    # candidates, counted from 0, and logs their names.
    out.append('static int kept_wanted(const char *field, unsigned k)')
    out.append('{')
    out.append('\tconst char *range = getenv("RDN_GLD_KEPT");')
    out.append('\tunsigned first = 0, last = ~0u;')
    out.append('')
    out.append('\tif (!range)')
    out.append('\t\treturn 1;')
    out.append('\tsscanf(range, "%u-%u", &first, &last);')
    out.append('\tif (k < first || k > last)')
    out.append('\t\treturn 0;')
    out.append('\trdn_log("kept: candidate %u, %s, wrapped", k, field);')
    out.append('\treturn 1;')
    out.append('}')
    out.append('')
    out.append('static void kept_wrap(GLIFunctionDispatch *disp, unsigned entries)')
    out.append('{')
    out.append('\tstruct _CGLContextObject *cgl = (void *)((char *)disp -')
    out.append('\t\toffsetof(struct _CGLContextObject, disp));')
    out.append('\tstruct kept_table *t = kept_table(cgl->rend);')
    out.append('\tunsigned n = 0, k = 0;')
    out.append('')
    # Not wrapped: buffer_parameteri_APPLE, the last entry of the SDK's
    # table. Tiger 10.4.11's engine keeps data of its own in that word and
    # follows it (aglSetFullScreen and aglSetInteger crashed at the
    # address of our wrapper's first instruction). And pad, no function.
    for ret, name, params, names in entries:
        if name not in ('swap_APPLE', 'buffer_parameteri_APPLE', 'pad') and name not in OWN:
            out.append('\tKEEP(%s, %d)' % (name, index[name]))
    out.append('\tif (n)')
    out.append('\t\trdn_log("kept: %u entries Mesa lacks wrapped in table %p of engine context %p (time %u for that address)",')
    out.append('\t\t\tn, (void *)disp, t->rend, ++t->wraps);')
    out.append('}')
    out.append('')
    out.append('void rdn_dispatch_kept_report(const char *when)')
    out.append('{')
    out.append('\tunsigned i, used = 0;')
    out.append('')
    out.append('\tfor (i = 0; i < %d; i++)' % len(entries))
    out.append('\t\tused += kept_calls[i] != 0;')
    out.append('\trdn_log("kept: %u of the entries Mesa lacks called so far (%s)", used, when);')
    out.append('\tfor (i = 0; i < %d; i++)' % len(entries))
    out.append('\t\tif (kept_calls[i])')
    out.append('\t\t\trdn_log("kept:   %s %lu", lookups[i].name, kept_calls[i]);')
    out.append('}')
    out.append('')
    out.append('unsigned rdn_dispatch_install(void *table, unsigned entries)')
    out.append('{')
    out.append('\tGLIFunctionDispatch *disp = table;')
    out.append('\tunsigned n = 0;')
    out.append('')
    for ret, name, params, names in entries:
        if name in OWN:
            out.append('\tif (FITS(%s)) { disp->%s = t_%s; n++; }' % (name, name, name))
            continue
        if name in VAR_OWN:
            out.append('\tif (var_on() && FITS(%s)) { disp->%s = t_%s; n++; }' % (name, name, name))
            continue
        if name in FENCE_OWN:
            out.append('\tif ((var_on() || apple_on()) && FITS(%s)) { disp->%s = t_%s; n++; }' % (name, name, name))
            continue
        if name in APPLE_OWN:
            out.append('\tif (apple_on() && FITS(%s)) { disp->%s = t_%s; n++; }' % (name, name, name))
            continue
        out.append('\tif (m_%s && FITS(%s)) { disp->%s = t_%s; n++; }' % (name, name, name, name))
    out.append('\tif (FITS(swap_APPLE) && disp->swap_APPLE != rdn_swap_entry) {')
    out.append('\t\tengine_swap = disp->swap_APPLE;')
    out.append('\t\tdisp->swap_APPLE = rdn_swap_entry;')
    out.append('\t}')
    out.append('\tif (__builtin_expect(rdn_logging, 0) && rdn_kept_now)')
    out.append('\t\tkept_wrap(disp, entries);')
    out.append('\treturn n;')
    out.append('}')
    out.append('')
    out.append('const unsigned rdn_dispatch_entries = %d;' % len(entries))
    open(sys.argv[2], 'w').write('\n'.join(out) + '\n')


if __name__ == '__main__':
    main()
