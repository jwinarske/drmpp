# SPDX-FileCopyrightText: (C) 2024 The drmpp Contributors
# SPDX-License-Identifier: Apache-2.0

incdirs = [
    '../include/drmpp',
    config_inc
]

drmpp_sources = [
    'input/seat.cc',
    'input/keyboard.cc',
    'info/info.cc',
]

drmpp_lib = library('drmpp',
                    drmpp_sources,
                    cpp_args : ['-DHAVE_GETFB2'],
                    #cpp_args : ['-DRAPIDJSON_HAS_STDSTRING'],
                    include_directories : incdirs,
                    dependencies : [
                        di_dep,
                        input_dep,
                        rapidjson_dep,
                        spdlog_dep,
                        drm_dep,
                        udev_dep,
                        xkbcommon_dep,
                        runtime,
                    ],
                    install : true)

drmp_dep = declare_dependency(link_with : drmpp_lib,
                              include_directories : incdirs,
                              dependencies : [
                                  di_dep,
                                  input_dep,
                                  rapidjson_dep,
                                  spdlog_dep,
                                  drm_dep,
                                  udev_dep,
                                  xkbcommon_dep,
                                  runtime
                              ])
