# SPDX-FileCopyrightText: (c) 2024 The drmpp Contributors
# SPDX-License-Identifier: Apache-2.0

egl_dep = dependency('egl', include_type : 'system', required : true)
glesv2_dep = dependency('glesv2', include_type : 'system', required : true)
dl_dep = cpp.find_library('dl', required : true)
math_dep = cpp.find_library('m', required : true)

if get_option('vulkan')
    imgui_proj = subproject('imgui')
    imgui_dep = imgui_proj.get_variable('imgui_dep')
    vulkan_dep = dependency('vulkan', include_type : 'system', required : true)
endif

cmake = import('cmake')

cxxopts_opts = cmake.subproject_options()
cxxopts_opts.set_install(false)
cxxopts_opts.add_cmake_defines(
    {
        'CMAKE_BUILD_TYPE' : 'MinSizeRelease',
        'CMAKE_POLICY_DEFAULT_CMP0156' : 'NEW',
        'CMAKE_POLICY_DEFAULT_CMP0159' : 'NEW'
    })
cxxopts = cmake.subproject('cxxopts', options : cxxopts_opts)
cxxopts_dep = cxxopts.dependency('cxxopts')

executable('drm-caps', ['drm_caps.cc'],
           include_directories : incdirs,
           dependencies : [
               cxxopts_dep,
               drmpp_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

executable('drm-input', ['drm_input.cc'],
           include_directories : incdirs,
           dependencies : [
               cxxopts_dep,
               drmpp_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

executable('drm-simple', ['drm_simple.cc'],
           include_directories : incdirs,
           dependencies : [
               cxxopts_dep,
               drmpp_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

executable('drm-compositor', ['drm_compositor.cc'],
           include_directories : incdirs,
           dependencies : [
               cxxopts_dep,
               drmpp_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

executable('drm-hotplug', ['drm_hotplug.cc'],
           include_directories : incdirs,
           dependencies : [
               cxxopts_dep,
               drmpp_dep,
               udev_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

executable('drm-modes', ['drm_modes.cc'],
           include_directories : incdirs,
           dependencies : [
               cxxopts_dep,
               drmpp_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

executable('drm-fb', ['drm_fb.cc'],
           include_directories : incdirs,
           dependencies : [
               cxxopts_dep,
               drmpp_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

executable('drm-cursor', ['drm_cursor.cc'],
           include_directories : incdirs,
           dependencies : [
               cxxopts_dep,
               drmpp_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

executable('drm-lz', ['drm_lz.cc'],
           include_directories : incdirs,
           dependencies : [
               cxxopts_dep,
               drmpp_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

executable('drm-gbm', ['drm_gbm.cc'],
           include_directories : incdirs,
           cpp_args : ['-DEGL_NO_X11'],
           dependencies : [
               cxxopts_dep,
               egl_dep,
               glesv2_dep,
               drmpp_dep,
               gbm_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

subdir('snake')

if get_option('vulkan')
    subdir('vulkan')
endif

if get_option('lvgl-examples')
    subdir('lvgl')
endif