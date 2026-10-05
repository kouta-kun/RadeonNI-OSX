/*
 * Wrapper around the 10.4 SDK's <assert.h>, which predates C11's
 * static_assert.
 *
 * Copyright (c) 2026 kouta-kun and Claude
 * SPDX-License-Identifier: MIT
 */
#include_next <assert.h>
#if !defined(__cplusplus) && !defined(static_assert)
#define static_assert _Static_assert
#endif
