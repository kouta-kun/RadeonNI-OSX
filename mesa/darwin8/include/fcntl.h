/*
 * Wrapper around the 10.4 SDK's <fcntl.h> that adds what Tiger lacks; see
 * ../tiger_compat.h.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */
#include_next <fcntl.h>
#include "../tiger_compat.h"
