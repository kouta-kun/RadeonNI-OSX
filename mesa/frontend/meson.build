# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT
#
# Off-screen frontend: Mesa's OSMesa frontend as of Mesa 25.0, the last
# release that had it. scripts/build-mesa.sh copies this directory into the
# Mesa tree as src/gallium/frontends/rdn.

inc_rdn_frontend = include_directories('include')

librdn_st = static_library(
  'rdn_st',
  'osmesa.c',
  include_directories : [
    inc_rdn_frontend, inc_include, inc_src, inc_gallium, inc_gallium_aux,
    inc_glapi, inc_mesa,
  ],
  gnu_symbol_visibility : 'hidden',
  dependencies : [idep_mesautil],
)
