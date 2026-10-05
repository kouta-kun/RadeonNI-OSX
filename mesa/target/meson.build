# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT
#
# librdngl: the off-screen frontend, Mesa's GL, r600 and the osx-gpu winsys
# in one shared library, for the Linux host. scripts/build-mesa.sh copies
# this directory into the Mesa tree as src/gallium/targets/rdn, with a copy
# of the hardware library in `hw` inside it.

rdn_hw = files(
  'hw/rdn_mem.c',
  'hw/rdn_gpu.c',
  'hw/rdn_cp.c',
  'hw/rdn_selftest.c',
)

librdngl = shared_library(
  'rdngl',
  files('rdn_target.c', 'rdn_device_linux.c'), rdn_hw,
  gnu_symbol_visibility : 'hidden',
  link_args : [ld_args_gc_sections],
  include_directories : [
    include_directories('.'), inc_rdn_winsys, inc_rdn_frontend,
    inc_include, inc_src, inc_gallium, inc_gallium_aux, inc_gallium_winsys,
    inc_gallium_drivers, inc_amd_common,
  ],
  # The hardware library defines errno names itself where it needs them.
  c_args : ['-Wno-missing-prototypes'],
  link_whole : [librdn_st, libglapi],
  link_with : [libr600, libmesa, libgallium, libgalliumvl, librdnwinsys],
  dependencies : [dep_thread, idep_mesautil, idep_nir],
  install : false,
)
