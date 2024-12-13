# SPDX-FileCopyrightText: (c) 2024 The drmpp Contributors
# SPDX-License-Identifier: Apache-2.0

project('drmpp', ['c', 'cpp'],
        version : '0.1.0',
        meson_version : '>=0.55.0',
        license : 'Apache-2.0',
        default_options : [
            'cpp_std=c++17',
            'b_lto=true'],
)

#########################
# Test
#########################
message(meson.current_source_dir())

#########################
# Version Variable
#########################
version_array = meson.project_version().split('.')
drmpp_major_version = version_array[0].to_int()
drmpp_minor_version = version_array[1].to_int()
drmpp_micro_version = version_array[2].to_int()

drmpp_api_version_string = '@0@'.format(drmpp_major_version)

drmpp_interface_age = drmpp_micro_version
drmpp_binary_age = 100 * drmpp_minor_version + drmpp_micro_version

# Maintain compatibility with libtool versioning
drmpp_soversion = 0
current = drmpp_binary_age - drmpp_interface_age
revision = drmpp_interface_age
drmpp_libversion = '@0@.@1@.@2@'.format(drmpp_soversion, current, revision)

drmpp_name = 'drmpp-' + drmpp_api_version_string
drmpp_full_name = 'libdrmpp-' + drmpp_api_version_string

#########################
# Package Config
#########################
prefix = get_option('prefix')
libdir = join_paths(prefix, get_option('libdir'))
devenv = environment()

pkgname = meson.project_name()
pkglibdir = join_paths(libdir, pkgname)

#########################
# Compiler
#########################
cpp = meson.get_compiler('cpp')
add_project_arguments(
    cpp.get_supported_arguments(['-Wno-unused-parameter', '-Wno-missing-field-initializers', ]),
    language : ['cpp'],
)

#########################
# Config Header
#########################

cdata = configuration_data()
cdata.set_quoted('VERSION', meson.project_version())

config_h = configure_file(
    input : 'config.h.meson',
    output : 'config.h',
    configuration : cdata
)
config_inc = include_directories('.')

#########################
# Dependencies
#########################

drm_dep = dependency('libdrm', include_type : 'system', required : true)
gbm_dep = dependency('gbm', include_type : 'system', required : true)
input_dep = dependency('libinput', include_type : 'system', required : true)
udev_dep = dependency('libudev', include_type : 'system', required : true)
xkbcommon_dep = dependency('xkbcommon', include_type : 'system', required : true)

libsync_proj = subproject('sync', default_options : ['default_library=static'])
libsync_dep = libsync_proj.get_variable('sync_dep')

bsdrm_proj = subproject('bsdrm', default_options : ['default_library=static'])
bsdrm_dep = bsdrm_proj.get_variable('bsdrm_dep')

di_proj = subproject('libdisplay-info', default_options : ['default_library=static'])
di_dep = di_proj.get_variable('di_dep')

liftoff_proj = subproject('libliftoff', default_options : ['default_library=static'])
liftoff_dep = liftoff_proj.get_variable('liftoff')

runtime = cpp.find_library('rt', required : true)
dl = cpp.find_library('dl', required : true)

rapidjson = subproject('rapidjson')
rapidjson_dep = rapidjson.get_variable('rapidjson')

cmake = import('cmake')

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

if get_option('vulkan')
    vulkan_headers = cmake.subproject('Vulkan-Headers')
    vulkan_headers_dep = vulkan_headers.dependency('Vulkan-Headers')
endif

#########################
# Source
#########################

subdir('src')

if get_option('examples')
    subdir('examples')
endif

if get_option('tests')
    subdir('tests')
endif

if meson.version().version_compare('>=0.58.0')
    meson.add_devenv(devenv)
endif

install_headers('include/drmpp/drmpp.h')
