# SPDX-FileCopyrightText: (c) 2024 The drmpp Contributors
# SPDX-License-Identifier: Apache-2.0

egl_dep = dependency('egl', include_type : 'system', required : true)
opengl_dep = dependency('opengl', include_type : 'system', required : true)
dl_dep = dependency('dl', include_type : 'system', required : true)

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
           install : false,
)

executable('drm-gbm', ['drm_gbm.cc'],
           include_directories : incdirs,
           cpp_args : ['-DEGL_NO_X11'],
           dependencies : [
               cxxopts_dep,
               egl_dep,
               opengl_dep,
               drmp_dep,
               gbm_dep,
           ],
           install : false,
)

executable('drm-snake', ['drm_snake.cc', 'snake/snake.cc'],
           include_directories : incdirs,
           cpp_args : ['-DEGL_NO_X11'],
           dependencies : [
               cxxopts_dep,
               egl_dep,
               opengl_dep,
               drmp_dep,
               gbm_dep,
           ],
           install : false,
)

if get_option('vulkan')
    executable('vk-khr-inp', [
                   'vulkan/vk_khr_inp.cc',
               ],
               include_directories : [incdirs],
               cpp_args : [],
               dependencies : [
                   cxxopts_dep,
                   drmp_dep,
                   vulkan_dep,
                   vulkan_headers_dep,
                   dl_dep,
               ],
               install : false,
    )

    executable('vk-kms-inp', [
                   'vulkan/vk_kms_inp.cc',
               ],
               include_directories : [incdirs],
               cpp_args : [],
               dependencies : [
                   cxxopts_dep,
                   drmp_dep,
                   vulkan_dep,
                   vulkan_headers_dep,
                   dl_dep,
               ],
               install : false,
    )
endif