# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT
#
# The osx-gpu winsys for r600. scripts/build-mesa.sh copies this directory
# into the Mesa tree as src/gallium/winsys/rdn.
#
# Mesa's own surface layout sources are built unchanged. They are copied
# first, so that their quoted includes find the stand-ins in shim/ instead
# of the DRM winsys headers that sit next to the originals.

fs = import('fs')
rdn_surface_c = fs.copyfile('../radeon/drm/radeon_surface.c', 'rdn_copy_radeon_surface.c')
rdn_drm_surface_c = fs.copyfile('../radeon/drm/radeon_drm_surface.c',
                                'rdn_copy_radeon_drm_surface.c')

inc_rdn_winsys = include_directories('.')

librdnwinsys = static_library(
  'rdnwinsys',
  files('rdn_winsys.c'), rdn_surface_c, rdn_drm_surface_c,
  include_directories : [
    include_directories('shim'), inc_rdn_winsys,
    include_directories('../radeon/drm'),
    inc_src, inc_include, inc_gallium, inc_gallium_aux, inc_amd_common,
  ],
  c_args : [no_override_init_args],
  gnu_symbol_visibility : 'hidden',
  dependencies : [idep_mesautil],
)
