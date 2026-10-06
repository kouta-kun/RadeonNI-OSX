/*
 * Engine and memory clocks.
 *
 * ASIC_Init leaves the card in its boot state, which on this family is a
 * slow one (FirmwareInfo's default clocks). The states the card is meant
 * to work in are in the VBIOS's PowerPlay table. This sets one of them
 * through the VBIOS's own command tables: voltage, engine clock, memory
 * clock. It is the static switch of Linux's radeon_set_power_state(), not
 * its dynamic power management: no SMC microcode, nothing changes by
 * itself afterwards.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_PM_H
#define RDN_PM_H

#include "rdn_card.h"

struct rdn_pm_state {
	uint32_t sclk;		/* engine clock, 10 kHz units */
	uint32_t mclk;		/* memory clock, 10 kHz units */
	uint16_t vddc;		/* core voltage in mV; 0 leaves it alone */
	uint16_t vddci;		/* I/O voltage in mV; 0 leaves it alone */
};

/* What rdn_pm_set() may change. */
#define RDN_PM_VOLTAGE		(1 << 0)
#define RDN_PM_SCLK		(1 << 1)
#define RDN_PM_MCLK		(1 << 2)
#define RDN_PM_ALL		(RDN_PM_VOLTAGE | RDN_PM_SCLK | RDN_PM_MCLK)

/* The state ASIC_Init leaves (FirmwareInfo). */
int rdn_pm_boot_state(struct rdn_card *card, struct rdn_pm_state *state);

/*
 * The fastest state in the PowerPlay table: the clock mode with the
 * highest engine clock, and of those the highest memory clock.
 */
int rdn_pm_performance_state(struct rdn_card *card, struct rdn_pm_state *state);

/* Log every state and clock mode of the PowerPlay table. */
void rdn_pm_log_states(struct rdn_card *card);

/* The clocks the card runs at now, as the VBIOS reads them back. */
int rdn_pm_get_clocks(struct rdn_card *card, uint32_t *sclk, uint32_t *mclk);

/*
 * Go from `from` to `to`, changing only what `what` names. The voltage is
 * raised before the clocks and lowered after them. The caller keeps the
 * command processor idle and lets nobody else touch the card meanwhile.
 * Returns 0 or a negative errno value.
 */
int rdn_pm_set(struct rdn_card *card, const struct rdn_pm_state *from,
	       const struct rdn_pm_state *to, unsigned what);

/* The GPU's own temperature sensor, in thousandths of a degree Celsius. */
int rdn_pm_temperature(struct rdn_card *card);

#endif /* RDN_PM_H */
