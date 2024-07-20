# SPDX-FileCopyrightText: (C) 2024 drmpp contributors
# SPDX-License-Identifier: Apache-2.0

incdirs = [
    '../include/drmpp',
    '../buildDir',
    '../subprojects/rapidjson-v1.1.0/include'
]

drmpp_sources = [
    'kms/info/info.cc']

drmpp_lib = shared_library('drmpp',
                           drmpp_sources,
                           cpp_args : ['-DRAPIDJSON_HAS_STDSTRING', '-DHAVE_GETFB2'],
                           include_directories : incdirs,
                           dependencies : [drm_dep, spdlog_dep],
                           install : true)

drmp_dep = declare_dependency(link_with : drmpp_lib,
                              include_directories : incdirs,
                              dependencies : [drm_dep, spdlog_dep])