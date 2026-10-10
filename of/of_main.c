/*
 * Open Firmware client for the Radeon HD 7570: the same POST and mode set
 * as the kext, run from Open Firmware before any OS (docs/OPEN-FIRMWARE.md,
 * route C).  This file is only an OS layer for hw/, a client-interface
 * wrapper and a front end; the card logic is hw/'s.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../hw/rdn_os.h"
#include "../hw/rdn_card.h"
#include "../hw/rdn_mode.h"
#include "../hw/rdn_pattern.h"
#include "../hw/rdn_reg.h"
#include "../hw/rdn_i2c.h"
#include "../hw/linux/evergreend.h"

#ifndef STAGE
#define STAGE 99
#endif
#define ROM_MAX		0x20000u	/* the largest ROM image copied */
#define APER_MAP	0x02000000u	/* 32 MB of the aperture */

/* The card, as found in the device tree by find_card() */
static char card_path[192], parent_path[192];
static uint32_t card_cfg;		/* config-space address: bus, device, function */
static uint32_t aper_hi, aper_lo, aper_size;	/* BAR 0 */
static uint32_t reg_hi, reg_lo, reg_size;	/* BAR 2, registers */
static uint32_t rom_hi, rom_lo, rom_size;	/* expansion ROM BAR */
static unsigned fb_w, fb_h, fb_pitch;		/* the mode that was set */

/* ---- client interface ------------------------------------------------ */

uint32_t of_entry;

struct of_args {
	const char *service;
	int nargs, nret;
	uint32_t a[16];
};

static int prom(const char *svc, int nargs, int nret, const uint32_t *in,
		uint32_t *out)
{
	struct of_args x;
	int i;

	x.service = svc;
	x.nargs = nargs;
	x.nret = nret;
	for (i = 0; i < nargs; i++)
		x.a[i] = in[i];
	for (i = nargs; i < nargs + nret; i++)
		x.a[i] = 0;
	if (((int (*)(struct of_args *))of_entry)(&x))
		return -1;
	for (i = 0; i < nret; i++)
		out[i] = x.a[nargs + i];
	return 0;
}

static uint32_t stdout_ih;

static void con_write(const char *s, int n)
{
#ifdef SILENT
	return;
#endif
	uint32_t in[3] = { stdout_ih, (uint32_t)s, (uint32_t)n }, out[1];

	prom("write", 3, 1, in, out);
}

static void puts_crlf(const char *s)
{
	char b[300];
	size_t n = strlen(s);

	if (n > 60) {		/* short writes only */
		char t[64];

		memcpy(t, s, 60);
		t[60] = 0;
		puts_crlf(t);
		puts_crlf(s + 60);
		return;
	}
	memcpy(b, s, n);
	b[n++] = '\r';
	b[n++] = '\n';
	con_write(b, (int)n);
}

static uint32_t of_open(const char *path)
{
	uint32_t in[1] = { (uint32_t)path }, out[1];

	if (prom("open", 1, 1, in, out))
		return 0;
	return out[0];
}

static uint32_t of_finddevice(const char *path)
{
	uint32_t in[1] = { (uint32_t)path }, out[1];

	if (prom("finddevice", 1, 1, in, out))
		return (uint32_t)-1;
	return out[0];
}

static int of_getprop(uint32_t ph, const char *name, void *buf, int len)
{
	uint32_t in[4] = { ph, (uint32_t)name, (uint32_t)buf, (uint32_t)len }, out[1];

	if (prom("getprop", 4, 1, in, out))
		return -1;
	return (int)out[0];
}

/* call-method: stack inputs are given top first; returns the catch result. */
static int of_call(const char *method, uint32_t ih, int nin, const uint32_t *args,
		   int nout, uint32_t *rets)
{
	uint32_t in[16], out[8];
	int i;

	in[0] = (uint32_t)method;
	in[1] = ih;
	for (i = 0; i < nin; i++)
		in[2 + i] = args[i];
	if (prom("call-method", 2 + nin, 1 + nout, in, out))
		return -1;
	for (i = 0; i < nout; i++)
		rets[i] = out[1 + i];
	return (int)out[0];
}

static uint32_t of_child(uint32_t ph)
{
	uint32_t in[1] = { ph }, out[1];

	return prom("child", 1, 1, in, out) ? 0 : out[0];
}

static uint32_t of_peer(uint32_t ph)
{
	uint32_t in[1] = { ph }, out[1];

	return prom("peer", 1, 1, in, out) ? 0 : out[0];
}

/* The Radeon HD 7570 (1002:675d): the same card the kext matches. */
static uint32_t find_card_in(uint32_t ph, int depth)
{
	uint32_t v, d, n, r;

	for (; ph && ph != (uint32_t)-1; ph = of_peer(ph)) {
		v = d = 0;
		if (of_getprop(ph, "vendor-id", &v, 4) == 4 && v == 0x1002 &&
		    of_getprop(ph, "device-id", &d, 4) == 4 && d == 0x675d)
			return ph;
		n = depth < 12 ? of_child(ph) : 0;
		if (n && (r = find_card_in(n, depth + 1)))
			return r;
	}
	return 0;
}

/* Fill card_path, parent_path, card_cfg and the BAR addresses. 0 on success. */
static int find_card(void)
{
	uint32_t cells[48], ph, in[3], out[1];
	int n, i, len;

	ph = find_card_in(of_child(of_finddevice("/")), 0);
	if (!ph)
		return -1;
	in[0] = ph;
	in[1] = (uint32_t)card_path;
	in[2] = sizeof(card_path) - 1;
	if (prom("package-to-path", 3, 1, in, out) || (int)out[0] <= 1)
		return -2;
	len = (int)out[0];
	if (len > (int)sizeof(card_path) - 1)
		len = sizeof(card_path) - 1;
	card_path[len] = 0;
	memcpy(parent_path, card_path, len + 1);
	for (i = len - 1; i > 0 && parent_path[i] != '/'; i--)
		;
	parent_path[i] = 0;		/* the bridge the card sits behind */

	n = of_getprop(ph, "reg", cells, sizeof(cells));
	if (n < 20)
		return -3;
	card_cfg = cells[0] & 0x00ffff00u;

	n = of_getprop(ph, "assigned-addresses", cells, sizeof(cells));
	for (i = 0; n >= 20 && i + 5 <= n / 4; i += 5) {
		switch (cells[i] & 0xff) {
		case 0x10: aper_hi = cells[i]; aper_lo = cells[i + 2]; aper_size = cells[i + 4]; break;
		case 0x18: reg_hi = cells[i]; reg_lo = cells[i + 2]; reg_size = cells[i + 4]; break;
		case 0x30: rom_hi = cells[i]; rom_lo = cells[i + 2]; rom_size = cells[i + 4]; break;
		}
	}
	return (aper_lo && reg_lo) ? 0 : -4;
}

/* ---- the PCI bus and the card --------------------------------------- */

static uint32_t bus_ih;
static volatile uint8_t *regs, *aper;

static uint32_t cfg_read(uint32_t addr)
{
	uint32_t a[1] = { addr }, r[1] = { 0xffffffffu };

	of_call("config-l@", bus_ih, 1, a, 1, r);
	return r[0];
}

static void cfg_write(uint32_t addr, uint32_t v)
{
	uint32_t a[2] = { addr, v };

	of_call("config-l!", bus_ih, 2, a, 0, NULL);
}

static void *map_in(uint32_t hi, uint32_t lo, uint32_t size)
{
	uint32_t a[4] = { size, hi, 0, lo }, r[1] = { 0 };

	if (of_call("map-in", bus_ih, 4, a, 1, r))
		return NULL;
	return (void *)r[0];
}

static inline uint32_t tb_lo(void)
{
	uint32_t v;

	__asm__ volatile("mftb %0" : "=r"(v));
	return v;
}

static uint32_t tb_hz = 33265212u;

static void delay_us(void *c, uint32_t us)
{
	uint64_t ticks = (uint64_t)us * tb_hz / 1000000u + 1;
	uint32_t start = tb_lo();

	while ((uint32_t)(tb_lo() - start) < ticks)
		;
}

static uint64_t time_ms(void *c)
{
	uint32_t hi, lo, hi2;

	do {
		__asm__ volatile("mftbu %0" : "=r"(hi));
		__asm__ volatile("mftb %0" : "=r"(lo));
		__asm__ volatile("mftbu %0" : "=r"(hi2));
	} while (hi != hi2);
	return (((uint64_t)hi << 32) | lo) / (tb_hz / 1000u);
}

static uint32_t mmio_read32(void *c, uint32_t off)
{
	uint32_t v;
	volatile uint8_t *p = regs + off;

	__asm__ volatile("lwbrx %0,0,%1; twi 0,%0,0; isync"
			 : "=r"(v) : "r"(p) : "memory");
	return v;
}

static void mmio_write32(void *c, uint32_t off, uint32_t v)
{
	volatile uint8_t *p = regs + off;

	__asm__ volatile("stwbrx %0,0,%1; eieio" : : "r"(v), "r"(p) : "memory");
}

static uint32_t cfg_read32(void *c, uint32_t off)
{
	return cfg_read(card_cfg | off);
}

static void cfg_write32(void *c, uint32_t off, uint32_t v)
{
	cfg_write(card_cfg | off, v);
}

/* 2 MB of heap for the library; nothing is ever freed. */
static uint8_t *heap;
#define HEAP_BYTES (2u << 20)
static size_t heap_used;

static void *os_alloc(void *c, size_t n)
{
	void *p;

	n = (n + 15) & ~(size_t)15;
	if (!heap || heap_used + n > HEAP_BYTES)
		return NULL;
	p = heap + heap_used;
	heap_used += n;
	return p;			/* .bss is zero, and nothing is reused */
}

static void os_free(void *c, void *p) { }

/* A small printf: %d %u %x %X %s %c %p, l/ll/z, width and zero padding. */
static int vfmt(char *o, int max, const char *f, va_list ap)
{
	int n = 0;

#define PUT(ch) do { if (n < max - 1) o[n] = (ch); n++; } while (0)
	for (; *f; f++) {
		int zero = 0, w = 0, lng = 0, neg = 0;
		unsigned long long v;
		char t[24];
		int k = 0;

		if (*f != '%') { PUT(*f); continue; }
		f++;
		if (*f == '0') { zero = 1; f++; }
		while (*f >= '0' && *f <= '9') w = w * 10 + *f++ - '0';
		while (*f == 'l' || *f == 'z' || *f == 'h') { if (*f != 'h') lng++; f++; }
		switch (*f) {
		case 's': {
			const char *s = va_arg(ap, const char *);
			if (!s) s = "(null)";
			while (*s) { PUT(*s); s++; }
			continue; }
		case 'c': PUT((char)va_arg(ap, int)); continue;
		case '%': PUT('%'); continue;
		case 'd': case 'i': {
			long long s = lng > 1 ? va_arg(ap, long long) : va_arg(ap, int);
			if (lng == 1 && sizeof(long) == 4) s = (int)s;
			if (s < 0) { neg = 1; v = -s; } else v = s;
			break; }
		case 'u': v = lng > 1 ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned); break;
		case 'p': v = (uintptr_t)va_arg(ap, void *); PUT('0'); PUT('x'); goto hex;
		case 'x': case 'X':
			v = lng > 1 ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned);
		hex:
			do { int d = v & 15; t[k++] = d < 10 ? '0' + d : 'a' + d - 10; v >>= 4; } while (v);
			goto emit;
		default: PUT('?'); continue;
		}
		do { t[k++] = '0' + (int)(v % 10); v /= 10; } while (v);
	emit:
		if (neg) t[k++] = '-';
		while (k < w) t[k++] = zero ? '0' : ' ';
		while (k) { PUT(t[--k]); }
	}
	if (max > 0)
		o[n < max ? n : max - 1] = 0;
	return n;
#undef PUT
}

/* The log: to the console, and kept in a ring for a later look. */
static char logring[8192];
static unsigned logpos;

static void os_log(void *c, enum rdn_log_level lvl, const char *fmt, va_list ap)
{
	char b[256];
	int n;

	if (lvl > RDN_LOG_INFO)
		return;
	n = vfmt(b, sizeof(b), fmt, ap);
	if (n > 255)
		n = 255;
	for (int i = 0; i < n; i++)
		logring[logpos++ % sizeof(logring)] = b[i];
	logring[logpos++ % sizeof(logring)] = '\n';
	puts_crlf(b);
}

static void say(const char *fmt, ...)
{
	char b[256];
	va_list ap;

	va_start(ap, fmt);
	vfmt(b, sizeof(b), fmt, ap);
	va_end(ap);
	puts_crlf(b);
}

/* Breadcrumb in NVRAM (variable rdn-step): survives a hang, read from Tiger. */
static void crumb(const char *what)
{
#ifdef SILENT
	return;
#endif
	static char b[96];
	uint32_t in[1], out[2];
	size_t n = 0, i;

	b[n++] = '"'; b[n++] = ' ';
	for (i = 0; what[i] && n < 40; i++)
		b[n++] = what[i];
	b[n++] = '"'; b[n++] = ' ';
	memcpy(b + n, "\" rdn-step\" $setenv", 18);
	n += 18;
	b[n] = 0;
	in[0] = (uint32_t)b;
	prom("interpret", 1, 2, in, out);
}

/* The VBIOS: enable the ROM BAR, copy the image through its mapping, disable it. */
static uint8_t bios_copy[ROM_MAX];	/* in the image, like all statics */

static int read_rom(void)
{
	volatile uint8_t *rom;
	uint32_t bar = cfg_read(card_cfg | 0x30);
	uint32_t i, len, sum;

	if (!rom_lo || !rom_size)
		return -1;
	if (rom_size > ROM_MAX)
		rom_size = ROM_MAX;
	rom = map_in(rom_hi, rom_lo, rom_size);
	if (!rom)
		return -1;
	cfg_write(card_cfg | 0x30, rom_lo | 1);
	if (rom[0] != 0x55 || rom[1] != 0xaa) {
		cfg_write(card_cfg | 0x30, bar & ~1u);
		return -2;
	}
	len = (uint32_t)rom[2] * 512;
	if (len < 0x1000 || len > rom_size) {
		cfg_write(card_cfg | 0x30, bar & ~1u);
		return -3;
	}
	/* whole words: some ROM BARs misbehave for byte reads */
	for (i = 0; i < len; i += 4) {
		uint32_t w = *(volatile uint32_t *)(rom + i);

		bios_copy[i] = w >> 24;
		bios_copy[i + 1] = w >> 16;
		bios_copy[i + 2] = w >> 8;
		bios_copy[i + 3] = w;
	}
	cfg_write(card_cfg | 0x30, bar & ~1u);
	for (sum = 0, i = 0; i < len; i++)
		sum += bios_copy[i];
	return (sum & 0xff) ? -4 : (int)len;	/* option ROM images sum to 0 */
}

static struct rdn_card card;

static void status(const char *tag)
{
	say("%s: memsize %x, crtc0 %x, grph %x, frames %x", tag,
	    (unsigned)rdn_rreg(&card, 0x5428),
	    (unsigned)rdn_rreg(&card, EVERGREEN_CRTC_CONTROL),
	    (unsigned)rdn_rreg(&card, EVERGREEN_GRPH_ENABLE),
	    (unsigned)rdn_rreg(&card, CRTC_STATUS_FRAME_COUNT));
}


#ifdef CONSOLE_NODE
/*
 * Forth for the display node: Open Firmware's console on this card (8 bpp).
 * Numbers are hex: 780 = 1920, 438 = 1080.  The node is created and finished
 * by the first chunk; every later chunk selects it again with `dev`, because
 * the selected device does not survive from one `interpret` call to the next.
 * Chunks stay well below 1000 characters.
 */
static const char *const console_chunks[] = {
	/* 0: the node, its properties and data */
	"dev {{P}} \" display\" device-type "
	"0 value line-bytes 0 value width 0 value height 0 value rdn-uses 0 value rdn-bus "
	"0 value rdn-no 0 value rdn-nc 0 value rdn-nl "
	"{{W}} encode-int \" width\" property {{H}} encode-int \" height\" property "
	"8 encode-int \" depth\" property {{LB}} encode-int \" linebytes\" property "
	"{{AP}} encode-int \" address\" property "
	"0 value rdn-ra 0 value rdn-rx 0 value rdn-ry 0 value rdn-rw 0 value rdn-rh "
	"create rdn-pal 300 allot "
	"dev / ",
	/* 1: geometry and rectangles */
	"dev {{P}} "
	": dimensions ( -- w h ) width height ; "
	": fill-rectangle ( idx x y w h -- ) to rdn-rh to rdn-rw to rdn-ry to rdn-rx to rdn-ra "
	"rdn-rh 0 ?do frame-buffer-adr rdn-ry i + line-bytes * + rdn-rx + rdn-rw rdn-ra fill loop ; "
	": draw-rectangle ( adr x y w h -- ) to rdn-rh to rdn-rw to rdn-ry to rdn-rx to rdn-ra "
	"rdn-rh 0 ?do rdn-ra i rdn-rw * + frame-buffer-adr rdn-ry i + line-bytes * + rdn-rx + rdn-rw move loop ; "
	": read-rectangle ( adr x y w h -- ) to rdn-rh to rdn-rw to rdn-ry to rdn-rx to rdn-ra "
	"rdn-rh 0 ?do frame-buffer-adr rdn-ry i + line-bytes * + rdn-rx + rdn-ra i rdn-rw * + rdn-rw move loop ; "
	"dev / ",
	/* 2: colours: a software copy and the card's colour table (Apple's boot code loads its palette here) */
	"dev {{P}} "
	": rdn-w ( val reg base -- ) + swap lbflip swap l! ; "
	": rdn-lut ( adr start cnt -- ) \" {{PP}}\" open-dev to rdn-bus "
	"{{RP}} 0 {{RH}} {{RS}} \" map-in\" rdn-bus $call-method >r "
	"0 69e0 r@ rdn-w 7 69f8 r@ rdn-w over 69e4 r@ rdn-w nip "
	"0 ?do dup i 3 * + dup c@ 16 lshift over 1+ c@ c lshift or swap 2+ c@ 2 lshift or "
	"69f0 r@ rdn-w loop drop r> drop rdn-bus close-dev ; "
	": rdn-sw! ( r g b n -- ) 3 * rdn-pal + >r r@ 2+ c! r@ 1+ c! r> c! ; "
	": color! ( r g b n -- ) dup >r rdn-sw! r> dup 3 * rdn-pal + swap 1 rdn-lut ; "
	": color@ ( n -- r g b ) 3 * rdn-pal + >r r@ c@ r@ 1+ c@ r> 2+ c@ ; "
	": set-colors ( adr n cnt -- ) to rdn-rh to rdn-rx to rdn-ra "
	"rdn-ra rdn-rx 3 * rdn-pal + rdn-rh 3 * move rdn-ra rdn-rx rdn-rh rdn-lut ; "
	": get-colors ( adr n cnt -- ) 3 * >r 3 * rdn-pal + swap r> move ; "
	"dev / ",
	/* 3: event markers, memory decode, open */
	"dev {{P}} "
	": rdn-mark ( x y -- ) \" {{PP}}\" open-dev to rdn-bus "
	"{{AP}} 0 {{AH}} 200000 \" map-in\" rdn-bus $call-method "
	"swap {{LB}} * + + 14 0 do 1e 0 do ff over j {{LB}} * + i + c! loop loop drop "
	"rdn-bus close-dev ; "
	": rdn-mem ( on? -- ) \" {{PP}}\" open-dev to rdn-bus "
	"{{C4}} \" config-w@\" rdn-bus $call-method swap if 2 or else fff9 and then "
	"{{C4}} \" config-w!\" rdn-bus $call-method rdn-bus close-dev ; "
	": open ( -- ok? ) true rdn-mem {{AP}} to frame-buffer-adr {{LB}} to line-bytes {{W}} to width "
	"{{H}} to height default-font set-font width height width char-width / "
	"height char-height / fb8-install 255 to foreground-color "
	"0 to background-color 100 0 do i i i i rdn-sw! loop rdn-uses 1+ to rdn-uses "
	"rdn-no 1+ dup to rdn-no 28 * 190 rdn-mark true ; "
	"dev / ",
	/* 4: close and the text writer */
	"dev {{P}} "
	": rdn-last ( -- ) rdn-nl 1+ dup to rdn-nl 28 * 1f4 rdn-mark "
	"\" {{PP}}\" open-dev to rdn-bus "
	"4f46524e lbflip {{RP}} 0 {{RH}} {{RS}} \" map-in\" rdn-bus $call-method 851c + l! "
	"rdn-bus close-dev false rdn-mem ; "
	": close ( -- ) rdn-nc 1+ dup to rdn-nc 28 * 1c2 rdn-mark rdn-uses 1- dup to rdn-uses 0= if rdn-last then ; "
	": rnl ( -- ) 0 to column# line# 1+ dup #lines >= if drop 0 to line# else to line# then ; "
	"0 value esc "
	": put1 ( c -- ) esc 1 = if 5b = if 2 to esc else 0 to esc then exit then "
	"esc 2 = if 40 >= if 0 to esc then exit then "
	"dup 1b = if drop 1 to esc exit then "
	"dup 0d = if drop 0 to column# exit then "
	"dup 0a = if drop rnl exit then "
	"dup 20 < if drop exit then "
	"draw-character column# 1+ dup #columns >= if drop rnl else to column# then ; "
	": write ( addr len -- actual ) dup 0 ?do over i + c@ put1 loop nip ; "
	"dev / ",
	/* 5: make it the console */
	"\" devalias screen {{P}}\" evaluate "
	"\" {{P}}\" output \" keyboard\" input "
	"\" Open Firmware console on the Radeon HD 7570. \" type cr ",
	0
};

static void cmark(unsigned x, unsigned y)
{
	unsigned r, c;

	for (r = 0; r < 20; r++)
		for (c = 0; c < 30; c++)
			aper[(y + r) * fb_pitch + x + c] = 0xff;
}

static void hexs(char *d, uint32_t v)
{
	static const char digits[] = "0123456789abcdef";
	char t[9];
	int n = 0;

	do {
		t[n++] = digits[v & 15];
		v >>= 4;
	} while (v);
	while (n)
		*d++ = t[--n];
	*d = 0;
}

/* Copy a chunk, replacing {{NAME}} by the discovered values (hex numbers, paths). */
static void expand(const char *t, char *out, size_t max)
{
	size_t n = 0;

	while (*t && n + 40 < max) {
		if (t[0] == '{' && t[1] == '{') {
			char name[6], val[200];
			const char *e = t + 2;
			size_t k = 0;

			while (*e && *e != '}' && k < 5)
				name[k++] = *e++;
			name[k] = 0;
			if (e[0] == '}' && e[1] == '}') {
				if (!strcmp(name, "P")) { strncpy(val, card_path, sizeof(val) - 1); val[sizeof(val) - 1] = 0; }
				else if (!strcmp(name, "PP")) { strncpy(val, parent_path, sizeof(val) - 1); val[sizeof(val) - 1] = 0; }
				else if (!strcmp(name, "AP")) hexs(val, aper_lo);
				else if (!strcmp(name, "AH")) hexs(val, aper_hi);
				else if (!strcmp(name, "RP")) hexs(val, reg_lo);
				else if (!strcmp(name, "RH")) hexs(val, reg_hi);
				else if (!strcmp(name, "RS")) hexs(val, reg_size);
				else if (!strcmp(name, "C4")) hexs(val, card_cfg | 4);
				else if (!strcmp(name, "W")) hexs(val, fb_w);
				else if (!strcmp(name, "H")) hexs(val, fb_h);
				else if (!strcmp(name, "LB")) hexs(val, fb_pitch);
				else val[0] = 0;
				for (k = 0; val[k] && n + 2 < max; k++)
					out[n++] = val[k];
				t = e + 2;
				continue;
			}
		}
		out[n++] = *t++;
	}
	out[n] = 0;
}

static void make_console(void)
{
	static char chunk[2400];
	uint32_t in[1], out[2];
	int i;

	cmark(40, 350);			/* reached the interpret calls */
	for (i = 0; console_chunks[i]; i++) {
		expand(console_chunks[i], chunk, sizeof(chunk));
		in[0] = (uint32_t)chunk;
		out[0] = (uint32_t)-1;
		prom("interpret", 1, 2, in, out);
		if (out[0] != 0)
			break;			/* a chunk failed: stop here */
		cmark(80 + 40 * i, 350);	/* chunk i went through */
	}
}
#endif

extern char _start[], _stack_top[];

/* The client interface's exit: control goes back to whoever ran `go`. Returning
 * with blr jumps to address 0 (go sets no LR) and Open Firmware then aborts the
 * whole command line. */
void of_exit(void)
{
	uint32_t out[1];

#ifdef CHAIN_BOOT
	{			/* in a boot-command line, returning aborts the line: boot from here */
		static const char cmd[] = "mac-boot";
		uint32_t in[1] = { (uint32_t)cmd }, o2[2];

		prom("interpret", 1, 2, in, o2);
	}
#endif

	prom("exit", 0, 0, out, out);
}

/*
 * Give back what Open Firmware claimed for us: the loader claims the image and
 * never releases it, and BootX needs its own ranges for the kernel. How much
 * the loader claims is not the segment's size, and releasing memory that is
 * already free corrupts the free list, so the gap in /memory's "available"
 * list that holds an address is what gets released.
 */
static void release_gap(uint32_t a)
{
	uint32_t av[64], in[2], out[1], mem = of_finddevice("/memory");
	int n, i;
	uint32_t lo = 0, hi = 0;

	if (mem == (uint32_t)-1)
		return;
	n = of_getprop(mem, "available", av, sizeof(av));
	if (n < 8)
		return;
	n /= 8;
	for (i = 0; i < n; i++) {		/* the free ranges, ascending */
		uint32_t s = av[2 * i], e = av[2 * i] + av[2 * i + 1];

		if (s <= a && a < e)
			return;			/* already free */
		if (e <= a && e > lo)
			lo = e;
		if (s > a && (!hi || s < hi))
			hi = s;
	}
	if (!hi || hi <= lo || a < lo || a >= hi)
		return;
	in[0] = lo;
	in[1] = hi - lo;
	prom("release", 2, 0, in, out);
}

static void give_back(void)
{
	/* the buffer the loader read this file into (load-base) stays claimed, and
	 * mac-boot loads BootX into the same address */
	release_gap(0x800000);
	/* not our own image: code that runs from released memory faults; the
	 * image is released from Forth after `go` */
	if (heap) {			/* our own claim, of known size */
		uint32_t in[2] = { (uint32_t)heap, HEAP_BYTES }, out[1];

		prom("release", 2, 0, in, out);
	}
}

static int rdn_main(void)
{
	static struct rdn_os os;
	static uint8_t edid[RDN_EDID_MAX_SIZE];
	struct rdn_mode mode;
	struct rdn_fb fb;
	uint32_t chosen, tb;
	int len, r;
	bool hdmi;
	uint32_t in[1], out[1];

	{	/* the heap: claimed from Open Firmware, zeroed */
		uint32_t ci[3] = { 0, HEAP_BYTES, 0x1000 }, co[1] = { 0 };

		ci[0] = 0;
		if (!prom("claim", 3, 1, ci, co) && co[0] != (uint32_t)-1)
			heap = (uint8_t *)co[0];
		if (heap)
			memset(heap, 0, HEAP_BYTES);
	}
	if (STAGE < 0)		/* null client: claims and gives back, touches nothing */
		return 0;
	chosen = of_finddevice("/chosen");
	crumb("start");
	of_getprop(chosen, "stdout", &stdout_ih, 4);
	puts_crlf("rdn: Open Firmware client for the Radeon HD 7570");
	tb = of_finddevice("/cpus/PowerPC,G5@0");
	if (tb != (uint32_t)-1 && of_getprop(tb, "timebase-frequency", &out[0], 4) == 4 && out[0])
		tb_hz = out[0];
	(void)in;

	if (find_card()) {
		puts_crlf("rdn: no Radeon HD 7570 in the device tree");
		return 1;
	}
	say("rdn: card %s", card_path);
	bus_ih = of_open(parent_path);
	if (!bus_ih) {
		puts_crlf("rdn: cannot open the card's bridge");
		return 1;
	}
	crumb("bridge-open");
	say("rdn: id %x", (unsigned)cfg_read(card_cfg));
	/* memory decode and bus master on: Open Firmware leaves memory off */
#ifdef FB8
	/* the display needs memory decode only (NVIDIA's driver never sets bus master) */
	cfg_write(card_cfg | 0x04, (cfg_read(card_cfg | 0x04) & 0xffffu) | 2);
#else
	cfg_write(card_cfg | 0x04, (cfg_read(card_cfg | 0x04) & 0xffffu) | 6);
#endif

	regs = map_in(reg_hi, reg_lo, reg_size);
	aper = map_in(aper_hi, aper_lo, aper_size < APER_MAP ? aper_size : APER_MAP);
	say("rdn: regs %p aperture %p", (void *)regs, (void *)aper);
	if (!regs || !aper)
		return 2;

	crumb("maps-done");
	len = read_rom();
	crumb("rom-read");
	say("rdn: vbios %d bytes", len);
	if (len < 0)
		return 3;
	crumb("sum-start");
	{
		uint32_t sum = 0;
		int k;

		for (k = 0; k < len; k++)
			sum += bios_copy[k];
		say("rdn: vbios sum %x, first %x %x %x %x, atom magic at 0x30: %x %x",
		    (unsigned)sum, bios_copy[0], bios_copy[1], bios_copy[2], bios_copy[3],
		    bios_copy[0x30], bios_copy[0x31]);
	}
	crumb("sum-done");
	if (STAGE < 1) {
		puts_crlf("rdn: stage 0 done");
		return 0;
	}
	puts_crlf("rdn: card init");
	crumb("card-init");

	os.cookie = NULL;
	os.mmio_read32 = mmio_read32;
	os.mmio_write32 = mmio_write32;
	os.cfg_read32 = cfg_read32;
	os.cfg_write32 = cfg_write32;
	os.delay_us = delay_us;
	os.time_ms = time_ms;
	os.alloc = os_alloc;
	os.free = os_free;
	os.log = os_log;
	if (rdn_card_init(&card, &os, bios_copy)) {
		puts_crlf("rdn: no usable VBIOS");
		return 4;
	}
	say("rdn: posted before: %d", (int)rdn_card_posted(&card));
	status("before");
	if (STAGE < 2) {
		puts_crlf("rdn: stage 1 done");
		return 0;
	}
	crumb("post");
	r = rdn_card_post(&card);
	crumb("post-done");
	say("rdn: post returned %d", r);
	status("after post");
	if (r || STAGE < 3)
		goto out;
#if defined(TAIL) && TAIL == 3
	goto out;		/* experiment C: POST only, no mode set */
#endif

	len = rdn_output_detect(&card, edid);
	if (len < 0 || !rdn_edid_preferred_mode(edid, &mode)) {
		say("rdn: no EDID (%d)", len);
		goto out;
	}
	hdmi = rdn_edid_is_hdmi(edid, len) && !card.output->displayport;
	say("rdn: %s, EDID %d bytes, %ux%u at %u kHz", card.output->name, len,
	    mode.hdisplay, mode.vdisplay, (unsigned)mode.clock);

	memset(&fb, 0, sizeof(fb));
	fb.width = mode.hdisplay;
	fb.height = mode.vdisplay;
	fb.pitch_pixels = (mode.hdisplay + 63u) & ~63u;
	fb_w = fb.width;
	fb_h = fb.height;
	fb_pitch = fb.pitch_pixels;
	fb.big_endian_pixels = RDN_BIG_ENDIAN;
#ifdef FB8
	fb.bpp = 8;			/* Open Firmware's text words are 8 bit */
	{
		volatile uint32_t *p = (volatile uint32_t *)aper;
		size_t w, nw = (size_t)fb.pitch_pixels * fb.height / 4;

		for (w = 0; w < nw; w++)
			p[w] = 0;		/* index 0: black in the linear ramp */
	}
	say("rdn: fb8 at %x, %u x %u, linebytes %u", (unsigned)aper_lo, fb.width, fb.height,
	    fb.pitch_pixels);
#else
	rdn_pattern_draw((volatile uint32_t *)aper, fb.width, fb.height, fb.pitch_pixels);
#endif
	crumb("display-init");
	r = rdn_display_init(&card);
	say("rdn: display init %d", r);
	if (!r) {
		crumb("modeset");
		r = rdn_modeset(&card, &mode, &fb, hdmi);
		crumb("modeset-done");
		say("rdn: modeset returned %d", r);
	}
	status("after modeset");
	delay_us(0, 200000);
	status("200 ms later");
#ifndef FB8
	if (!r) {
		rdn_handover_mark(&card);
		puts_crlf("rdn: hand-over marker set");
	}
#endif
#if (!defined(TAIL) && !defined(FB8)) || (defined(TAIL) && TAIL == 1)
	/* experiment A: leave the mode running but decode and bus master off */
	cfg_write(card_cfg | 0x04, cfg_read(card_cfg | 0x04) & ~6u);
#elif defined(TAIL) && TAIL == 2
	/* experiment B: stop the scanout (power the output down), POST stays */
	rdn_output_disable(&card, &mode, hdmi);
	status("scanout off");
#endif
out:
#ifdef CONSOLE_NODE
	if (!r)
		make_console();
#endif
	return r;
}

/* Whatever happens inside, the memory claimed from Open Firmware is given back. */
int of_main(void)
{
	int r = rdn_main();

	give_back();
	return r;
}
