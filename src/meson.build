# SPDX-FileCopyrightText: (c) 2024 The drmpp Contributors
# SPDX-License-Identifier: Apache-2.0

di_proj = subproject('libdisplay-info', default_options : ['default_library=static'])
di_dep = di_proj.get_variable('di_dep')

liftoff_proj = subproject('libliftoff', default_options : ['default_library=static'])
liftoff_dep = liftoff_proj.get_variable('liftoff')

gbm_dep = dependency('gbm', include_type : 'system', required : true)
drm_dep = dependency('libdrm', include_type : 'system', required : true)
input_dep = dependency('libinput', include_type : 'system', required : true)
udev_dep = dependency('libudev', include_type : 'system', required : true)
xkbcommon_dep = dependency('xkbcommon', include_type : 'system', required : true)

runtime = cpp.find_library('rt', required : true)
dl = cpp.find_library('dl', required : true)

rapidjson = subproject('rapidjson')
rapidjson_dep = rapidjson.get_variable('rapidjson')

cmake = import('cmake')

if get_option('vulkan')
    vulkan_headers = cmake.subproject('Vulkan-Headers')
    vulkan_headers_dep = vulkan_headers.dependency('Vulkan-Headers')
endif

spdlog_opts = cmake.subproject_options()
spdlog_opts.set_install(false)
spdlog_opts.add_cmake_defines(
    {
        'CMAKE_BUILD_TYPE' : 'MinSizeRelease',
        'SPDLOG_NO_EXCEPTIONS' : true,
        'SPDLOG_NO_THREAD_ID' : true,
        'SPDLOG_BUILD_PIC' : true,
        'CMAKE_POLICY_DEFAULT_CMP0156' : 'NEW'
    }
)
spdlog = cmake.subproject('spdlog', options : spdlog_opts)
spdlog_dep = spdlog.dependency('spdlog')

incdirs = [
    '../include',
    '../include/drmpp',
    config_inc
]

drmpp_sources = [
    'cursor/xcursor.cc',
    'kms/device.cc',
    'kms/output.cc',
    'input/seat.cc',
    'input/keyboard.cc',
    'input/pointer.cc',
    'input/touch.cc',
    'input/fastlz.cc',
    'info/info.cc',
    'plane/plane.cc',
    'shared_libs/libdrm.cc',
    'shared_libs/libegl.cc',
    'shared_libs/libgbm.cc',
    'utils/udev_monitor.cc',
    'utils/virtual_terminal.cc',
]

drmpp_dep_deps = [
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
]

if get_option('vulkan')
    drmpp_sources += 'vulkan/vulkan_base.cc'
    drmpp_sources += 'vulkan/vulkan_khr.cc'
    drmpp_sources += 'vulkan/vulkan_kms.cc'
    drmpp_dep_deps += vulkan_headers_dep
endif

version = meson.project_version().split('-')[0]
version_minor = version.split('.')[1]

drmpp_lib = library('drmpp',
                    drmpp_sources,
                    cpp_args : ['-Wno-unused-private-field'],
                    include_directories : incdirs,
                    version : version,
                    soversion : version_minor,
                    dependencies : drmpp_dep_deps,
                    install : true)

install_subdir(
    'include/libdrmpp',
    install_dir : get_option('includedir'),
)

pkgconfig = import('pkgconfig')
pkgconfig.generate(
    drmpp_lib,
    filebase : 'libdrmpp',
    name : 'libdrmpp',
    description : 'DRM C++ library',
    url : 'https://github.com/jwinarske/drmpp',
)

drmpp_dep = declare_dependency(link_with : drmpp_lib,
                               include_directories : incdirs,
                               dependencies : drmpp_dep_deps)

meson.override_dependency('libdrmpp', drmpp_dep)
