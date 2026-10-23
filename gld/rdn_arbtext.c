/*
 * See rdn_arbtext.h.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#include "rdn_arbtext.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LITERALS 64
#define NAME_MAX_LEN 48

struct literal {
	char name[NAME_MAX_LEN];
	double v[4];
};

static int is_id(int c)
{
	return isalnum((unsigned char)c) || c == '_' || c == '$';
}

/* Skip blanks and tabs. */
static const char *blank(const char *p, const char *end)
{
	while (p < end && (*p == ' ' || *p == '\t'))
		p++;
	return p;
}

/* An identifier at p: its length, 0 when there is none. */
static size_t ident(const char *p, const char *end)
{
	size_t n = 0;

	if (p < end && !isdigit((unsigned char)*p))
		while (p + n < end && is_id(p[n]))
			n++;
	return n;
}

static int word(const char *p, const char *end, const char *w)
{
	size_t n = strlen(w);

	return p + n <= end && !strncmp(p, w, n) && (p + n == end || !is_id(p[n]));
}

/* "PARAM name = { a, b, c, d };" -> the literal, 0 if it is not one. */
static int parse_literal(const char *p, const char *end, struct literal *lit)
{
	size_t n;
	char tmp[256], *q, *e;
	int i;

	p = blank(p, end);
	if (!word(p, end, "PARAM"))
		return 0;
	p = blank(p + 5, end);
	n = ident(p, end);
	if (!n || n >= NAME_MAX_LEN)
		return 0;
	memcpy(lit->name, p, n);
	lit->name[n] = 0;
	p = blank(p + n, end);
	if (p >= end || *p != '=')
		return 0;
	p = blank(p + 1, end);
	if (p >= end || *p != '{')
		return 0;
	p++;
	for (n = 0; p + n < end && p[n] != '}' && n < sizeof(tmp) - 1; n++)
		tmp[n] = p[n];
	if (p + n >= end || p[n] != '}')
		return 0;
	tmp[n] = 0;
	lit->v[0] = lit->v[1] = 0;
	lit->v[2] = 0;
	lit->v[3] = 1;
	q = tmp;
	for (i = 0; i < 4; i++) {
		double d = strtod(q, &e);

		if (e == q)
			break;
		lit->v[i] = d;
		q = e;
		while (*q == ' ' || *q == '\t')
			q++;
		if (*q == ',')
			q++;
		else
			break;
	}
	return i > 0 || e != tmp;
}

static int component(char c)
{
	switch (c) {
	case 'x': case 'r': case 's': return 0;
	case 'y': case 'g': case 't': return 1;
	case 'z': case 'b': case 'p': return 2;
	case 'w': case 'a': case 'q': return 3;
	}
	return -1;
}

/*
 * "ALIAS name = rhs;" in [p, end): what it becomes, appended to out
 * (capacity checked by the caller through the worst-case size). Returns
 * the number of bytes written, 0 for "leave it".
 */
static size_t rewrite_alias(const char *p, const char *end,
			    const struct literal *lits, int nlits, char *out)
{
	const char *name, *rhs, *semi;
	size_t nlen, i;
	const char *kw = NULL;

	p = blank(p, end);
	if (!word(p, end, "ALIAS"))
		return 0;
	p = blank(p + 5, end);
	name = p;
	nlen = ident(p, end);
	if (!nlen)
		return 0;
	p = blank(p + nlen, end);
	if (p >= end || *p != '=')
		return 0;
	rhs = blank(p + 1, end);
	semi = memchr(rhs, ';', end - rhs);
	if (!semi)
		return 0;

	if (!strncmp(rhs, "vertex.", 7) || !strncmp(rhs, "fragment.", 9))
		kw = "ATTRIB";
	else if (!strncmp(rhs, "program.", 8) || !strncmp(rhs, "state.", 6))
		kw = "PARAM";
	else if (!strncmp(rhs, "result.", 7))
		kw = "OUTPUT";
	if (kw)
		return (size_t)sprintf(out, "%s %.*s = %.*s;", kw, (int)nlen, name,
				       (int)(semi - rhs), rhs);

	/* name.c of a literal PARAM */
	{
		size_t rl = ident(rhs, semi);

		if (rl && rhs[rl] == '.') {
			const char *s = rhs + rl + 1;
			int c = s < semi ? component(*s) : -1;

			if (c >= 0 && blank(s + 1, semi) == semi) {
				for (i = 0; i < (size_t)nlits; i++)
					if (strlen(lits[i].name) == rl &&
					    !memcmp(lits[i].name, rhs, rl)) {
						double v = lits[i].v[c];

						return (size_t)sprintf(out,
							"PARAM %.*s = { %.9g, %.9g, %.9g, %.9g };",
							(int)nlen, name, v, v, v, v);
					}
			}
		}
	}
	return 0;
}

char *rdn_arb_alias_rewrite(const char *text, int *len)
{
	struct literal lits[MAX_LITERALS];
	int nlits = 0, changed = 0;
	const char *end, *line;
	char *out;
	size_t cap, o = 0;

	if (!text || !len || *len <= 0)
		return NULL;
	end = text + *len;
	/* Each rewritten statement is at most 80 bytes more than itself. */
	{
		size_t nl = 0;
		const char *q;

		for (q = text; q < end; q++)
			if (*q == '\n')
				nl++;
		cap = (size_t)*len + (nl + 1) * 96 + 1;
	}
	out = malloc(cap);
	if (!out)
		return NULL;

	for (line = text; line < end; ) {
		const char *nl = memchr(line, '\n', end - line);
		const char *le = nl ? nl : end;
		const char *s = blank(line, le);
		size_t w = 0;

		if (s < le && *s != '#') {
			struct literal lit;

			if (parse_literal(s, le, &lit) && nlits < MAX_LITERALS)
				lits[nlits++] = lit;
			w = rewrite_alias(s, le, lits, nlits, out + o + (s - line));
			if (w) {
				const char *semi = memchr(s, ';', le - s);

				memcpy(out + o, line, s - line);
				o += (s - line) + w;
				/* what follows the statement on its line */
				memcpy(out + o, semi + 1, le - (semi + 1));
				o += le - (semi + 1);
				changed = 1;
				if (nl)
					out[o++] = '\n';
				line = nl ? nl + 1 : end;
				continue;
			}
		}
		memcpy(out + o, line, le - line);
		o += le - line;
		if (nl)
			out[o++] = '\n';
		line = nl ? nl + 1 : end;
	}
	if (!changed) {
		free(out);
		return NULL;
	}
	out[o] = 0;
	*len = (int)o;
	return out;
}

/*
 * Direct3D 8/9 pixel shader 1.x byte code -> ARB fragment program text.
 *
 * The Sims 2's pixel shader path hands the tokens its own assembler made
 * to glProgramStringARB as if they were text (the length is a strlen of
 * the token stream, so a few bytes). The token layout is Direct3D's:
 * a version token (0xFFFF0Mnn), instructions (opcode in the low 16 bits,
 * then the destination, then the sources), 0x0000FFFF at the end.
 */

#define D3D_TEMP	0
#define D3D_INPUT	1
#define D3D_CONST	2
#define D3D_TEXTURE	3

struct d3dps {
	char *buf;
	size_t n, cap;
	unsigned defd;		/* constants given by def */
	unsigned envc;		/* constants read from program.env */
	int ok;
};

static void d3d_put(struct d3dps *p, const char *fmt, ...)
{
	va_list ap;
	int w;

	if (p->n + 256 > p->cap) {
		char *nb = realloc(p->buf, p->cap * 2);

		if (!nb) {
			p->ok = 0;
			return;
		}
		p->buf = nb;
		p->cap *= 2;
	}
	va_start(ap, fmt);
	w = vsnprintf(p->buf + p->n, 256, fmt, ap);
	va_end(ap);
	if (w < 0 || w >= 256)
		p->ok = 0;
	else
		p->n += w;
}

static int d3d_regtype(unsigned t)
{
	return ((t >> 28) & 7) | ((t >> 8) & 0x18);
}

/* "r0", "t1", ... of a source or destination register, or NULL */
static const char *d3d_reg(struct d3dps *p, unsigned t, char *name, int src)
{
	unsigned num = t & 0x7ff;

	switch (d3d_regtype(t)) {
	case D3D_TEMP:
		if (num > 7)
			break;
		sprintf(name, "r%u", num);
		return name;
	case D3D_TEXTURE:
		if (num > 7)
			break;
		sprintf(name, "t%u", num);
		return name;
	case D3D_INPUT:
		if (num > 1 || !src)
			break;
		return num ? "fragment.color.secondary" : "fragment.color.primary";
	case D3D_CONST:
		if (num > 31 || !src)
			break;
		if (!(p->defd & (1u << num)) && !(p->envc & (1u << num))) {
			p->envc |= 1u << num;
			d3d_put(p, "PARAM c%u = program.env[%u];\n", num, num);
		}
		sprintf(name, "c%u", num);
		return name;
	}
	return NULL;
}

/* a source operand into a string, emitting the modifier into temp s<k> */
static int d3d_src(struct d3dps *p, unsigned t, int k, char *out)
{
	char reg[32], sw[8];
	const char *r = d3d_reg(p, t, reg, 1);
	unsigned mod = (t >> 24) & 15, s = (t >> 16) & 255;
	static const char c[4] = { 'x', 'y', 'z', 'w' };
	int i;

	if (!r || !(t & 0x80000000u))
		return 0;
	for (i = 0; i < 4; i++)
		sw[i] = c[(s >> (2 * i)) & 3];
	sw[4] = 0;
	if (s == 0xe4)
		sw[0] = 0;
	/* ps 1.x swizzles are one replicated component: keep the full form */
	switch (mod) {
	case 0:
		sprintf(out, "%s%s%s", r, sw[0] ? "." : "", sw);
		return 1;
	case 1:
		sprintf(out, "-%s%s%s", r, sw[0] ? "." : "", sw);
		return 1;
	case 2:		/* bias */
		d3d_put(p, "SUB s%d, %s%s%s, 0.5;\n", k, r, sw[0] ? "." : "", sw);
		break;
	case 3:		/* bias, negated */
		d3d_put(p, "SUB s%d, 0.5, %s%s%s;\n", k, r, sw[0] ? "." : "", sw);
		break;
	case 4:		/* sign: (x - 0.5) * 2 */
	case 5:
		d3d_put(p, "MAD s%d, %s%s%s, 2.0, -1.0;\n", k, r, sw[0] ? "." : "", sw);
		if (mod == 5)
			d3d_put(p, "MOV s%d, -s%d;\n", k, k);
		break;
	case 6:		/* complement */
		d3d_put(p, "SUB s%d, 1.0, %s%s%s;\n", k, r, sw[0] ? "." : "", sw);
		break;
	case 7:		/* x2 */
	case 8:
		d3d_put(p, "MUL s%d, %s%s%s, %s2.0;\n", k, r, sw[0] ? "." : "", sw, mod == 8 ? "-" : "");
		break;
	case 11:
	case 12:
		d3d_put(p, "ABS s%d, %s%s%s;\n", k, r, sw[0] ? "." : "", sw);
		if (mod == 12)
			d3d_put(p, "MOV s%d, -s%d;\n", k, k);
		break;
	default:
		return 0;
	}
	sprintf(out, "s%d", k);
	return 1;
}

/* the destination with its write mask: "r0.xyz" */
static int d3d_dst(struct d3dps *p, unsigned t, char *out, int *sat, int *shift)
{
	char reg[32];
	const char *r = d3d_reg(p, t, reg, 0);
	unsigned m = (t >> 16) & 15;
	int s = (t >> 24) & 15;

	if (!r || !(t & 0x80000000u) || !m)
		return 0;
	if (s & 8)
		s -= 16;
	*shift = s;
	*sat = (t >> 20) & 1;
	if (m == 15)
		strcpy(out, r);
	else
		sprintf(out, "%s.%s%s%s%s", r, m & 1 ? "x" : "", m & 2 ? "y" : "", m & 4 ? "z" : "", m & 8 ? "w" : "");
	return 1;
}

char *rdn_d3d_ps_to_arb(const void *code, int *len)
{
	const unsigned *tok = code;
	struct d3dps p = { 0 };
	unsigned i = 1;
	int k;

	if ((tok[0] & 0xffff0000u) != 0xffff0000u || ((tok[0] >> 8) & 255) != 1)
		return NULL;		/* ps_1_x only */
	p.cap = 4096;
	p.buf = malloc(p.cap);
	if (!p.buf)
		return NULL;
	p.ok = 1;
	d3d_put(&p, "!!ARBfp1.0\nTEMP r0, r1, r2, r3, r4, r5, r6, r7, t0, t1, t2, t3, t4, t5, t6, t7, s0, s1, s2, s3;\n");
	for (; p.ok && i < 4096 && tok[i] != 0x0000ffffu; ) {
		unsigned op = tok[i] & 0xffff;
		char d[48], a[3][48];
		int sat, shift;

		i++;
		if (op == 0xfffe) {		/* comment: its length in dwords */
			i += (tok[i - 1] >> 16) & 0x7fff;
			continue;
		}
		if (op == 0)			/* nop */
			continue;
		if (op == 0xfffd)		/* phase */
			continue;
		if (op == 0x51) {		/* def c#, x, y, z, w */
			unsigned num = tok[i] & 0x7ff;
			float f[4];

			if (d3d_regtype(tok[i]) != D3D_CONST || num > 31) {
				p.ok = 0;
				break;
			}
			memcpy(f, &tok[i + 1], sizeof f);
			d3d_put(&p, "PARAM c%u = { %.9g, %.9g, %.9g, %.9g };\n", num, f[0], f[1], f[2], f[3]);
			p.defd |= 1u << num;
			i += 5;
			continue;
		}
		if (op == 0x42 || op == 0x40) {	/* tex t#, texcoord t# */
			char reg[32];
			unsigned num = tok[i] & 0x7ff;

			if (d3d_regtype(tok[i]) != D3D_TEXTURE || !d3d_reg(&p, tok[i], reg, 0) || num > 7) {
				p.ok = 0;
				break;
			}
			if (op == 0x42)
				d3d_put(&p, "TEX t%u, fragment.texcoord[%u], texture[%u], 2D;\n", num, num, num);
			else
				d3d_put(&p, "MOV t%u, fragment.texcoord[%u];\n", num, num);
			i++;
			continue;
		}
		if (op == 0x41) {		/* texkill t# */
			d3d_put(&p, "KIL fragment.texcoord[%u];\n", tok[i] & 7);
			i++;
			continue;
		}
		{
			static const struct { unsigned op; const char *name; int nsrc; } ops[] = {
				{ 1, "MOV", 1 }, { 2, "ADD", 2 }, { 3, "SUB", 2 }, { 4, "MAD", 3 }, { 5, "MUL", 2 },
				{ 8, "DP3", 2 }, { 9, "DP4", 2 }, { 0x12, "LRP", 3 }, { 0x50, "CND", 3 }, { 0x58, "CMP", 3 },
			};
			const char *name = NULL;
			int ns = 0;
			unsigned j;

			for (j = 0; j < sizeof ops / sizeof ops[0]; j++)
				if (ops[j].op == op) {
					name = ops[j].name;
					ns = ops[j].nsrc;
				}
			if (!name || !d3d_dst(&p, tok[i], d, &sat, &shift)) {
				p.ok = 0;
				break;
			}
			for (k = 0; k < ns; k++)
				if (!d3d_src(&p, tok[i + 1 + k], k, a[k])) {
					p.ok = 0;
					break;
				}
			if (!p.ok)
				break;
			i += 1 + ns;
			if (op == 0x50) {	/* cnd: src0 > 0.5 ? src1 : src2 */
				d3d_put(&p, "SUB s3, %s, 0.5;\nCMP%s %s, s3, %s, %s;\n", a[0], shift ? "" : (sat ? "_SAT" : ""), d, a[2], a[1]);
			} else if (op == 0x58) {	/* cmp: src0 >= 0 ? src1 : src2 */
				d3d_put(&p, "CMP%s %s, %s, %s, %s;\n", shift ? "" : (sat ? "_SAT" : ""), d, a[0], a[2], a[1]);
			} else {
				d3d_put(&p, "%s%s %s", name, shift ? "" : (sat ? "_SAT" : ""), d);
				for (k = 0; k < ns; k++)
					d3d_put(&p, ", %s", a[k]);
				d3d_put(&p, ";\n");
			}
			if (shift) {
				float sc = shift > 0 ? (float)(1 << shift) : 1.0f / (float)(1 << -shift);

				d3d_put(&p, "MUL%s %s, %s, %.9g;\n", sat ? "_SAT" : "", d, d, sc);
			}
		}
	}
	if (!p.ok || tok[i] != 0x0000ffffu) {
		free(p.buf);
		return NULL;
	}
	d3d_put(&p, "MOV result.color, r0;\nEND\n");
	if (!p.ok) {
		free(p.buf);
		return NULL;
	}
	p.buf[p.n] = 0;
	*len = (int)p.n;
	return p.buf;
}
