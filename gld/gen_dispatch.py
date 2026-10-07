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
APPLE_ONLY = {
    'pixel_storei': ('pname', (0x85B2,)),
    'pixel_storef': ('pname', (0x85B2,)),
    'tex_parameteri': ('pname', (0x85BC,)),
    'tex_parameterf': ('pname', (0x85BC,)),
    'tex_parameteriv': ('pname', (0x85BC,)),
    'tex_parameterfv': ('pname', (0x85BC,)),
    'hint': ('target', (0x85B1,)),
}

# Entries that carry data for a buffer object. With glthread, more than
# rdn_async_limit bytes in one call make the program's thread wait for all
# the calls before it; in pieces they do not (rdn_glue.h). The buffer ends
# up the same: glBufferData without data makes the store, glBufferSubData
# fills it.
SPLIT = {
    'buffer_data': """\
	if (__builtin_expect(rdn_async_limit && data && size > rdn_async_limit, 0) &&
	    m_buffer_sub_data) {
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
           '#include <string.h>',
           '#include <OpenGL/CGLContext.h>',
           '#include "rdn_dispatch.h"', '']
    index = dict((e[1], i) for i, e in enumerate(entries))
    for name in list(SPLIT) + ['flush', 'finish']:
        if name not in [e[1] for e in entries]:
            sys.exit('no entry %s' % name)
    # All of Mesa's first: an entry may call more than its own.
    for ret, name, params, names in entries:
        out.append('static %s (*m_%s)(%s);' % (ret, name, params if params else 'void'))
    out.append('')
    for ret, name, params, names in entries:
        full = 'GLIContext ctx' + (', ' + params if params else '')
        call = 'm_%s(%s)' % (name, ', '.join(names))
        out.append('static %s t_%s(%s)' % (ret, name, full))
        out.append('{')
        out.append('\tRDN_ENTER(ctx);')
        out.append('\tif (__builtin_expect(rdn_trace, 0) && rdn_trace_wanted("%s"))' % gl_name(name))
        out.append('\t\t%s;' % trace_call(name, params, names))
        if name in APPLE_ONLY:
            arg, values = APPLE_ONLY[name]
            if arg not in names:
                sys.exit('%s has no %s' % (name, arg))
            out.append('\tif (%s)' % ' || '.join('%s == 0x%X' % (arg, v) for v in values))
            out.append('\t\treturn;')
        if name in WINDOW_XY:
            if 'x' not in names or 'y' not in names:
                sys.exit('%s has no x and y' % name)
            out.append('\tx += rdn_origin_x;')
            out.append('\ty += rdn_origin_y;')
        if name in SPLIT:
            out.append(SPLIT[name])
        if name == 'flush':
            out.append(FLUSH)
        out.append('\t%s%s;' % ('' if ret == 'void' else 'return ', call))
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
    out.append('static void rdn_swap_entry(GLIContext ctx)')
    out.append('{')
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
        if name not in ('swap_APPLE', 'buffer_parameteri_APPLE', 'pad'):
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
