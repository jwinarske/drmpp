project(
    'sync',
    'c',
    version : '1.0.0',
    license : 'Apache-2.0',
    meson_version : '>=0.52.0',
)

sync_inc = include_directories('include')

sync_lib = library(
    'sync',
    files(
        'sync.c',
    ),
    include_directories: sync_inc,
    version: meson.project_version().split('-')[0],
    install: false,
)

sync_dep = declare_dependency(
    link_with: sync_lib,
    include_directories: sync_inc,
)