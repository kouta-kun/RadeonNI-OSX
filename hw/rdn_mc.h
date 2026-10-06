/*
 * The memory controller's microcode.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */

#ifndef RDN_MC_H
#define RDN_MC_H

#include "rdn_card.h"

/* TURKS_mc.bin: this many big-endian words. */
#define RDN_MC_UCODE_WORDS	6024

/*
 * Load the microcode of the memory controller's sequencer and let it train
 * the memory, as Linux does on a card with GDDR5 whose sequencer is not
 * running. Video memory is reset by this: call it after ASIC_Init and
 * before anything is kept there, with no CRTC scanning out.
 *
 * Returns 0 when the microcode was loaded and the training finished, 1
 * when there was nothing to do (not GDDR5, or the sequencer runs already),
 * or a negative errno value.
 */
int rdn_mc_load_microcode(struct rdn_card *card, const void *fw, size_t size);

#endif /* RDN_MC_H */
