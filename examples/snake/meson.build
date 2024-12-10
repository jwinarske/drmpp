# SPDX-FileCopyrightText: (c) 2024 The drmpp Contributors
# SPDX-License-Identifier: Apache-2.0

opengl_dep = dependency('opengl', include_type : 'system')

if opengl_dep.found()
    executable('drm-snake', ['drm_snake.cc', 'snake.cc'],
               cpp_args : ['-DEGL_NO_X11'],
               dependencies : [
                   cxxopts_dep,
                   egl_dep,
                   opengl_dep,
                   drmp_dep,
                   gbm_dep,
               ],
               install : true,
               install_dir : get_option('bindir'),
    )
endif