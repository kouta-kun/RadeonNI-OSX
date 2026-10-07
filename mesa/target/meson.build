# Copyright (c) 2026 kouta-kun and Claude
# SPDX-License-Identifier: MIT
#
# librdngl: the off-screen frontend, Mesa's GL, r600 and the osx-gpu winsys
# in one shared library, for the Linux host. scripts/build-mesa.sh copies
# this directory into the Mesa tree as src/gallium/targets/rdn, with a copy
# of the hardware library in `hw` inside it.

# The device: on Mac OS X the kext's user client, elsewhere (the Linux
# host) the hardware library in this process.
if host_machine.system() == 'darwin'
  # rdn_mem.c: the device divides up its chunks of GART memory itself.
  rdn_device = files('rdn_device_darwin.c', 'hw/rdn_mem.c')
  rdn_device_link_args = ['-framework', 'IOKit', '-framework', 'CoreFoundation']
else
  rdn_device = files(
    'rdn_device_linux.c',
    'hw/rdn_mem.c',
    'hw/rdn_gpu.c',
    'hw/rdn_cp.c',
    'hw/rdn_blit.c',
    'hw/rdn_selftest.c',
  )
  rdn_device_link_args = []
endif

rdn_inc = [
  include_directories('.'), inc_rdn_winsys, inc_rdn_frontend,
  inc_include, inc_src, inc_gallium, inc_gallium_aux, inc_gallium_winsys,
  inc_gallium_drivers, inc_amd_common,
]
rdn_sources = [files('rdn_target.c'), rdn_device]
rdn_link_whole = [librdn_st, libglapi]
rdn_link_with = [libr600, libmesa, libgallium, libgalliumvl, librdnwinsys]
rdn_deps = [dep_thread, idep_mesautil, idep_nir]

librdngl = shared_library(
  'rdngl',
  rdn_sources,
  gnu_symbol_visibility : 'hidden',
  link_args : [ld_args_gc_sections, rdn_device_link_args],
  include_directories : rdn_inc,
  link_whole : rdn_link_whole,
  link_with : rdn_link_with,
  dependencies : rdn_deps,
  install : false,
)

# The test program with everything linked in, so that it can be built
# static for a foreign architecture and run under qemu-user
# (-Dc_link_args=-static -Dcpp_link_args=-static).
#
# On Linux it can also run without the card (RDN_SOFT=1), on Mesa's
# software rasteriser with ordinary memory for video memory: enough to
# check what the frontend does with share lists and stores (rdn_gltest -P).
rdn_gltest_sources = [rdn_sources, files('tests/rdn_gltest.c')]
rdn_gltest_args = []
rdn_gltest_deps = rdn_deps
if host_machine.system() != 'darwin' and with_gallium_softpipe
  rdn_gltest_sources += files('tests/rdn_soft.c')
  rdn_gltest_args += '-DRDN_TARGET_SOFT'
  rdn_gltest_deps += driver_softpipe
endif
executable(
  'rdn_gltest',
  rdn_gltest_sources,
  c_args : rdn_gltest_args,
  include_directories : rdn_inc,
  link_args : rdn_device_link_args,
  link_whole : rdn_link_whole,
  link_with : rdn_link_with,
  dependencies : rdn_gltest_deps,
  install : false,
)

# The OpenGL driver bundle for Mac OS X: Apple's driver interface on the
# outside (gld/RadeonNIGLDriver.c), Mesa inside. The dispatch glue is
# generated from the SDK's header, at the place the toolchain container
# has it. scripts/gld.sh puts the result into a bundle in the guest.
if host_machine.system() == 'darwin'
  rdn_dispatch_c = custom_target(
    'rdn_dispatch_gen.c',
    input : 'gld/gen_dispatch.py',
    output : 'rdn_dispatch_gen.c',
    command : [
      prog_python, '@INPUT@',
      '/usr/local/MacOSX10.4u.sdk/System/Library/Frameworks/OpenGL.framework/Headers/gliDispatch.h',
      '@OUTPUT@',
    ],
  )
  shared_module(
    'RadeonNIGLDriver',
    files('gld/RadeonNIGLDriver.c', 'gld/rdn_mesa.c', 'gld/rdn_hook.c'), rdn_dispatch_c,
    rdn_sources,
    c_args : ['-DRDN_MESA'],
    gnu_symbol_visibility : 'hidden',
    include_directories : [rdn_inc, include_directories('gld')],
    link_args : rdn_device_link_args,
    link_whole : rdn_link_whole,
    link_with : rdn_link_with,
    dependencies : rdn_deps,
    name_prefix : '',
    install : false,
  )
endif
