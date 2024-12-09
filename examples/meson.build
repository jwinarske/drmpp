# SPDX-FileCopyrightText: (c) 2024 The drmpp Contributors
# SPDX-License-Identifier: Apache-2.0

egl_dep = dependency('egl', include_type : 'system', required : true)
opengl_dep = dependency('opengl', include_type : 'system', required : true)
gles_dep = dependency('glesv2', include_type : 'system', required : true)
dl_dep = dependency('dl', required : true)

if get_option('vulkan')
    imgui_proj = subproject('imgui')
    imgui_dep = imgui_proj.get_variable('imgui_dep')
    vulkan_dep = dependency('vulkan', include_type : 'system', required : true)
endif

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
               udev_dep,
           ],
           install : true,
)

executable('drm-modes', ['drm_modes.cc'],
           include_directories : incdirs,
           dependencies : [
               cxxopts_dep,
               drmp_dep,
           ],
           install : true,
)

executable('drm-fb', ['drm_fb.cc'],
           include_directories : incdirs,
           dependencies : [
               cxxopts_dep,
               drmp_dep,
           ],
           install : true,
)

executable('drm-cursor', ['drm_cursor.cc'],
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
           install : true,
)

executable('drm-gbm', ['drm_gbm.cc'],
           include_directories : incdirs,
           cpp_args : ['-DEGL_NO_X11'],
           dependencies : [
               cxxopts_dep,
               egl_dep,
               gles_dep,
               drmp_dep,
               gbm_dep,
           ],
           install : true,
)

subdir('snake')

if get_option('vulkan')
    subdir('vulkan')
endif

if get_option('lvgl-examples')
    subdir('lvgl')
endif