# SPDX-FileCopyrightText: (c) 2024 The drmpp Contributors
# SPDX-License-Identifier: Apache-2.0

di_proj = subproject('libdisplay-info')
di_dep = di_proj.get_variable('di_dep')

liftoff_proj = subproject('libliftoff')
liftoff_dep = liftoff_proj.get_variable('liftoff')

gbm_dep = dependency('gbm', include_type : 'system', required : true)
drm_dep = dependency('libdrm', include_type : 'system', required : true)
input_dep = dependency('libinput', include_type : 'system', required : true)
udev_dep = dependency('libudev', include_type : 'system', required : true)
xkbcommon_dep = dependency('xkbcommon', include_type : 'system', required : true)

runtime = cpp.find_library('rt', required : true)
dl = cpp.find_library('dl', required : true)

rapidjson_dep = declare_dependency(include_directories : include_directories('../subprojects/rapidjson/include'))

cmake = import('cmake')

opt_var = cmake.subproject_options()
opt_var.add_cmake_defines({ 'CMAKE_VERBOSE_MAKEFILE' : true })
opt_var.add_cmake_defines({ 'SPDLOG_NO_EXCEPTIONS' : true })
opt_var.add_cmake_defines({ 'SPDLOG_NO_THREAD_ID' : true })
opt_var.add_cmake_defines({ 'SPDLOG_BUILD_PIC' : true })
opt_var.add_cmake_defines({ 'CMAKE_POLICY_DEFAULT_CMP0156' : 'NEW' })
spdlog = cmake.subproject('spdlog', options : opt_var)
spdlog_dep = spdlog.dependency('spdlog')

incdirs = [
    '../include/drmpp',
    config_inc
]

drmpp_sources = [
    'cursor/xcursor.cc',
    'input/seat.cc',
    'input/keyboard.cc',
    'input/pointer.cc',
    'input/touch.cc',
    'input/fastlz.cc',
    'info/info.cc',
    'plane/plane.cc',
    'egl/libegl.cc',
    'drm/libdrm.cc',
    'gbm/libgbm.cc',
]

drmpp_lib = library('drmpp',
                    drmpp_sources,
#                    cpp_args : ['-DHAVE_GETFB2'],
                    include_directories : incdirs,
                    dependencies : [
                        di_dep,
                        input_dep,
                        liftoff_dep,
                        rapidjson_dep,
                        spdlog_dep,
                        gbm_dep,
                        drm_dep,
                        udev_dep,
                        xkbcommon_dep,
                        runtime,
                        dl,
                    ],
                    install : true)

drmp_dep = declare_dependency(link_with : drmpp_lib,
                              include_directories : incdirs,
                              dependencies : [
                                  di_dep,
                                  input_dep,
                                  liftoff_dep,
                                  rapidjson_dep,
                                  spdlog_dep,
                                  gbm_dep,
                                  drm_dep,
                                  udev_dep,
                                  xkbcommon_dep,
                                  runtime
                              ])
