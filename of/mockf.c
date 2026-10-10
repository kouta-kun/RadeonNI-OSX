/* Host test for of_main.c: a mock Open Firmware with a one-card device tree and a
 * dumb card (every register reads back what was written, 0 at first); run under
 * qemu-ppc.  Finds crashes and hangs in the client's own code and shows what it
 * discovers and what Forth it would interpret; nothing about the real card. */
#include <stdint.h>
#include <string.h>

extern uint32_t of_entry;
extern int of_main(void);

static uint8_t regs[0x20000] __attribute__((aligned(16)));
/* a fake ROM image: 55 AA, 64 KB, checksum byte fixed up at run time */
static uint8_t rom[0x20000] __attribute__((aligned(16)));
static uint8_t aper[0x2000000];
static uint32_t cmd = 0;

struct a { const char *svc; int n, r; uint32_t v[16]; };

static void sys_write(const void *b, unsigned n)
{
	register long r0 __asm__("r0") = 4, r3 __asm__("r3") = 1, r4 __asm__("r4") = (long)b, r5 __asm__("r5") = n;
	__asm__ volatile("sc" : "+r"(r3) : "r"(r0), "r"(r4), "r"(r5) : "memory", "cr0", "r6", "r7", "r8", "r9", "r10", "r11", "r12");
}

static void puts_(const char *s) { sys_write(s, strlen(s)); sys_write("\n", 1); }

/* phandles: 1 root, 2 chosen, 3 cpu, 10 ht, 11 bridge, 12 other device, 13 the card */
static const uint32_t assigned[] = {
	0x81080020, 0, 0, 0, 0x100,
	0xc3080010, 0, 0x90000000, 0, 0x10000000,
	0x83080018, 0, 0x80140000, 0, 0x20000,
	0x82080030, 0, 0x80120000, 0, 0x20000,
};
static const uint32_t regprop[] = { 0x00080000, 0, 0, 0, 0 };

static void fake_rom(void)
{
	unsigned i, sum = 0;

	rom[0] = 0x55; rom[1] = 0xaa; rom[2] = 128;
	for (i = 0; i < 0x10000 - 1; i++)
		sum += rom[i];
	rom[0xffff] = (uint8_t)(0x100 - (sum & 0xff));
}

int mockf(struct a *x)
{
	static int first = 1;

	if (first) { first = 0; fake_rom(); }
	const char *s = x->svc;
	if (!strcmp(s, "write")) { sys_write((void *)x->v[1], x->v[2]); x->v[3] = x->v[2]; return 0; }
	if (!strcmp(s, "finddevice")) {
		const char *p = (const char *)x->v[0];
		x->v[1] = !strcmp(p, "/") ? 1 : !strcmp(p, "/chosen") ? 2 : !strncmp(p, "/cpus", 5) ? 3 : 0;
		return 0;
	}
	if (!strcmp(s, "child")) { x->v[1] = x->v[0] == 1 ? 10 : x->v[0] == 10 ? 11 : x->v[0] == 11 ? 12 : 0; return 0; }
	if (!strcmp(s, "peer")) { x->v[1] = x->v[0] == 12 ? 13 : 0; return 0; }
	if (!strcmp(s, "package-to-path")) {
		const char *p = "/ht@0,f2000000/pci@5/pci1028,2b20@0";
		memcpy((char *)x->v[1], p, strlen(p) + 1);
		x->v[3] = strlen(p);
		return 0;
	}
	if (!strcmp(s, "getprop")) {
		uint32_t ph = x->v[0]; const char *n = (const char *)x->v[1]; void *buf = (void *)x->v[2];
		int len = -1;
		if (ph == 13 && !strcmp(n, "vendor-id")) { uint32_t t = 0x1002; memcpy(buf, &t, 4); len = 4; }
		else if (ph == 13 && !strcmp(n, "device-id")) { uint32_t t = 0x675d; memcpy(buf, &t, 4); len = 4; }
		else if (ph == 12 && !strcmp(n, "vendor-id")) { uint32_t t = 0x14e4; memcpy(buf, &t, 4); len = 4; }
		else if (ph == 12 && !strcmp(n, "device-id")) { uint32_t t = 0x1648; memcpy(buf, &t, 4); len = 4; }
		else if (ph == 13 && !strcmp(n, "reg")) { memcpy(buf, regprop, sizeof(regprop)); len = sizeof(regprop); }
		else if (ph == 13 && !strcmp(n, "assigned-addresses")) { memcpy(buf, assigned, sizeof(assigned)); len = sizeof(assigned); }
		else if (ph == 3 && !strcmp(n, "timebase-frequency")) { uint32_t t = 33265212; memcpy(buf, &t, 4); len = 4; }
		else if (ph == 2 && !strcmp(n, "stdout")) { uint32_t t = 7; memcpy(buf, &t, 4); len = 4; }
		x->v[4] = (uint32_t)len;
		return 0;
	}
	if (!strcmp(s, "open")) { x->v[1] = 5; return 0; }
	if (!strcmp(s, "interpret")) {
		puts_("--- interpret:");
		puts_((const char *)x->v[0]);
		x->v[1] = 0;
		return 0;
	}
	if (!strcmp(s, "call-method")) {
		const char *m = (const char *)x->v[0];
		x->v[2 + (x->n - 2)] = 0;
		uint32_t *r = &x->v[x->n];	/* catch result first */
		r[0] = 0;
		if (!strcmp(m, "config-l@")) {
			uint32_t off = x->v[2] & 0xff;
			r[1] = off == 0 ? 0x675d1002u : off == 4 ? cmd : 0;
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
