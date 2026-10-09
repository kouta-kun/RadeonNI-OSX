/* Host test for of_main.c: a mock Open Firmware and a dumb card (every
 * register reads back what was written, 0 at first); run under qemu-ppc.
 * Finds crashes and hangs in the client's own code, nothing about the card. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern uint32_t of_entry;
extern int of_main(void);

static uint8_t regs[0x20000] __attribute__((aligned(16)));
static uint8_t rom[0x20000];
static uint8_t aper[0x2000000];
static uint32_t cmd = 0;

struct a { const char *svc; int n, r; uint32_t v[16]; };

static int mock(struct a *x)
{
	const char *s = x->svc;
	if (!strcmp(s, "write")) { fwrite((void *)x->v[1], 1, x->v[2], stdout); x->v[3] = x->v[2]; return 0; }
	if (!strcmp(s, "finddevice")) { x->v[1] = 1; return 0; }
	if (!strcmp(s, "getprop")) { uint32_t t = 7; memcpy((void *)x->v[2], &t, 4); x->v[4] = 4; return 0; }
	if (!strcmp(s, "open")) { x->v[1] = 5; return 0; }
	if (!strcmp(s, "call-method")) {
		const char *m = (const char *)x->v[0];
		x->v[2 + (x->n - 2)] = 0;
		uint32_t *r = &x->v[x->n];	/* catch result first */
		r[0] = 0;
		if (!strcmp(m, "config-l@")) {
			uint32_t off = x->v[2] & 0xff;
			r[1] = off == 0 ? 0x675d1002u : off == 4 ? cmd : off == 0x30 ? 0 : 0;
		} else if (!strcmp(m, "config-l!")) {
			if ((x->v[2] & 0xff) == 4) cmd = x->v[3];
		} else if (!strcmp(m, "map-in")) {
			uint32_t hi = x->v[3];
			r[1] = (uint32_t)(uintptr_t)(hi == 0x83080018 ? regs : hi == 0x82080030 ? rom : aper);
		}
		return 0;
	}
	return -1;
}

int main(int argc, char **argv)
{
	FILE *f = fopen(argc > 1 ? argv[1] : "private/vbios.rom", "rb");
	if (!f || fread(rom, 1, 65536, f) < 65536) { puts("no vbios"); return 1; }
	setvbuf(stdout, NULL, _IONBF, 0);
	of_entry = (uint32_t)(uintptr_t)mock;
	return of_main();
}
