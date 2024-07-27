# SPDX-FileCopyrightText: (C) 2024 The drmpp Contributors
# SPDX-License-Identifier: Apache-2.0

cmake = import('cmake')

opt_var = cmake.subproject_options()
opt_var.add_cmake_defines({ 'CMAKE_VERBOSE_MAKEFILE' : true })
opt_var.add_cmake_defines({ 'CMAKE_POLICY_DEFAULT_CMP0156' : 'NEW' })
opt_var.add_cmake_defines({ 'CMAKE_POLICY_DEFAULT_CMP0159' : 'NEW' })
cxxopts = cmake.subproject('cxxopts', options : opt_var)
cxxopts_dep = cxxopts.dependency('cxxopts')


executable('drm-caps', ['drm_caps.cc'],
           include_directories : incdirs,
           dependencies : [
               cxxopts_dep,
               drmp_dep,
           ],
           install : true,
)

executable('drm-input', ['drm_input.cc'],
           include_directories : incdirs,
           dependencies : [
               cxxopts_dep,
               drmp_dep,
           ],
           install : true,
)

executable('drm-simple', ['drm_simple.cc'],
           include_directories : incdirs,
           dependencies : [
               cxxopts_dep,
               drmp_dep,
           ],
           install : true,
)

executable('drm-compositor', ['drm_compositor.cc'],
           include_directories : incdirs,
           dependencies : [
               cxxopts_dep,
               drmp_dep,
           ],
           install : true,
)

executable('drm-hotplug', ['drm_hotplug.cc'],
           include_directories : incdirs,
           dependencies : [
               cxxopts_dep,
               drmp_dep,
           ],
           install : true,
)

executable('drm-lz', ['drm_lz.cc'],
           include_directories : incdirs,
           dependencies : [
               cxxopts_dep,
               drmp_dep,
           ],
           install : false,
)