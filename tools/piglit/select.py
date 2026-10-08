#!/usr/bin/env python3
"""Pick the piglit tests that make sense for a Tiger OpenGL 2.1 driver.

  select.py profile-commands.txt... > selected.tsv

Input lines are "name<TAB>command" from piglit-print-commands.py (see
tools/piglit/README in docs/PIGLIT.md). A test is dropped when its name
belongs to a feature set above OpenGL 2.1 / GLSL 1.20 or to a window system
other than GLUT, or when its .shader_test file requires a higher GL or GLSL
version. Output: name<TAB>command, sorted and unique. Commands are rewritten
to be relative to the piglit root on the Mac ("bin/..." and "tests/...").

Copyright (c) 2026 kouta-kun and Claude
SPDX-License-Identifier: MIT
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.path.join(ROOT, "build", "piglit-darwin")
TESTS = os.path.join(ROOT, "third_party", "piglit", "tests")

DROP_NAME = re.compile(
    r"^(glx|wgl|egl|egl_|object namespace pollution|security)\b|"
    r"glsl-(1\.[3-9]|[2-9]\.|es-)|gl-[3-9]\.|!opengl [3-9]\.|@egl|"
    r"@(arb_(gpu_shader|tessellation|gl_spirv|compute|vertex_attrib_64bit|shader_storage|"
    r"bindless|shader_image|texture_gather|texture_view|direct_state|enhanced_layouts|"
    r"arrays_of_arrays|shader_precision|shader_texture_image|texture_multisample|"
    r"copy_image|sample_shading|uniform_buffer|geometry_shader|texture_cube_map_array|"
    r"separate_shader|shading_language_packing|shader_bit|texture_barrier|"
    r"shader_atomic|shader_clock|shader_viewport|viewport_array|"
    r"transform_feedback[23]?|texture_buffer|draw_indirect|draw_instanced|"
    r"clip_control|cull_distance|conservative|compatibility|"
    r"derivative_control|explicit_|fragment_layer|fragment_shader_interlock|"
    r"gpu_|indirect|internalformat|map_buffer_alignment|multi_bind|"
    r"occlusion_query2|pipeline|post_depth|program_interface|query_buffer|"
    r"robust|sparse|stencil_texturing|sync|tessellation|texture_compression_bptc|"
    r"texture_filter_minmax|texture_storage_multisample|vertex_attrib_binding|"
    r"vertex_type)|amd_|arm_|intel_|ovr_|mesa_|nv_|nvx_|oes_|khr_|ext_image_dma|"
    r"ext_gpu_shader4|ext_transform_feedback|ext_texture_array|ext_texture_integer|"
    r"ext_texture_buffer|ext_shader_framebuffer|ext_polygon_offset_clamp|ext_semaphore|"
    r"ext_memory|ext_external|ext_demote)",
)
VER = re.compile(r"^\s*(GL|GLSL)\s*(>=|<=|==|<|>)\s*(\d+)\.(\d+)", re.I)
ES = re.compile(r"^\s*GL_ES|^\s*GLSL_ES|\bGL_ARB_(?:gpu|tess)", re.I)


def shader_ok(path):
    try:
        text = open(path, errors="replace").read(8192)
    except OSError:
        return False
    in_req = False
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("["):
            in_req = s.lower() == "[require]"
            if not in_req and s.lower() != "[require]":
                if s.lower().startswith(("[vertex", "[fragment", "[geometry", "[test")):
                    break
            continue
        if not in_req:
            continue
        if ES.search(s):
            return False
        m = VER.match(s)
        if m:
            kind, op, ma, mi = m.group(1).upper(), m.group(2), int(m.group(3)), int(m.group(4))
            v = ma * 100 + mi
            if op in (">=", ">", "==") and v > (210 if kind == "GL" else 120):
                return False
    return True


def main():
    seen = {}
    for fn in sys.argv[1:]:
        for line in open(fn):
            name, _, cmd = line.rstrip("\n").partition("\t")
            if not cmd or name in seen:
                continue
            if DROP_NAME.search(name):
                continue
            cmd = cmd.replace("../../build/piglit-darwin/bin/", "bin/")
            cmd = cmd.replace(BUILD + "/tests/", "tests/").replace(BUILD + "/", "")
            cmd = cmd.replace(TESTS + "/", "tests/")
            m = re.search(r"(?:^| )(\S+\.shader_test)\b", cmd)
            if m:
                p = m.group(1)
                src = os.path.join(BUILD if p.startswith("generated") else ROOT,
                                   p) if False else None
                full = os.path.join(BUILD, p)
                if not os.path.exists(full):
                    full = os.path.join(os.path.dirname(TESTS), p)
                if not shader_ok(full):
                    continue
            seen[name] = cmd
    for name in sorted(seen):
        print(name + "\t" + seen[name])


main()
