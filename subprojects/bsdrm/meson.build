# Copyright 2016 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

project(
    'bsdrm',
    'c',
    version : '1.1.0',
    license : 'BSD',
    meson_version : '>=0.52.0',
    default_options : ['c_std=gnu99'],
)

drm_dep = dependency('libdrm', include_type : 'system', required : true)
egl_dep = dependency('egl', include_type : 'system', required : true)
gbm_dep = dependency('gbm', include_type : 'system', required : true)
glesv2_dep = dependency('glesv2', include_type : 'system', required : true)

incdir = include_directories('include')

bsdrm_sources = [
    'src/alloc.c',
    'src/app.c',
    'src/debug.c',
    'src/draw.c',
    'src/drm_connectors.c',
    'src/drm_fb.c',
    'src/drm_open.c',
    'src/drm_pipe.c',
    'src/egl.c',
    'src/gl.c',
    'src/kms.c',
    'src/mmap.c',
    'src/open.c',
    'src/pipe.c',
]

bsdrm_lib = static_library(
    'bsdrm',
    bsdrm_sources,
    include_directories : incdir,
    c_args : ['-DEGL_NO_X11'],
    dependencies : [gbm_dep, drm_dep, egl_dep, glesv2_dep],
    install : true
)

bsdrm_dep = declare_dependency(
    link_with : bsdrm_lib,
    include_directories : incdir,
    dependencies : [gbm_dep, drm_dep, egl_dep, glesv2_dep],
)

subdir('example')
