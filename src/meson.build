# SPDX-FileCopyrightText: (C) 2024 drmpp contributors
# SPDX-License-Identifier: Apache-2.0

incdirs = [
    '../include/drmpp',
    '../subprojects/rapidjson-v1.1.0/include',
    config_inc
]

drmpp_sources = [
    'input/seat.cc',
    'input/keyboard.cc',
    'info/info.cc',
]

drmpp_lib = shared_library('drmpp',
                           drmpp_sources,
                           #cpp_args : ['-DHAVE_GETFB2'],
                           include_directories : incdirs,
                           dependencies : [drm_dep, input_dep, spdlog_dep, udev_dep, xkbcommon_dep],
                           link_args : '-lrt',
                           install : true)

drmp_dep = declare_dependency(link_with : drmpp_lib,
                              include_directories : incdirs,
                              dependencies : [drm_dep, input_dep, spdlog_dep, udev_dep, xkbcommon_dep])
