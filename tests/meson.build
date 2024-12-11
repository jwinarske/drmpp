executable('plane-test', ['plane_test.cc'],
           include_directories : incdirs,
           dependencies : [
               bsdrm_dep,
               math_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

executable('drm-cursor-test', ['drm_cursor_test.cc'],
           include_directories : incdirs,
           dependencies : [
               bsdrm_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

executable('swrast-test', ['swrast_test.cc'],
           include_directories : incdirs,
           dependencies : [
               bsdrm_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

executable('gamma-test', ['gamma_test.cc'],
           include_directories : incdirs,
           dependencies : [
               bsdrm_dep,
               math_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

executable('linear-bo-test', ['linear_bo_test.cc'],
           include_directories : incdirs,
           dependencies : [
               bsdrm_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

pthread_dep = dependency('threads')

executable('atomic-test', ['atomictest.cc'],
           include_directories : incdirs,
           dependencies : [
               bsdrm_dep,
               drmpp_dep,
               libsync_dep,
               pthread_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

executable('mapped-texture-test', ['mapped_texture_test.cc'],
           include_directories : incdirs,
           dependencies : [
               bsdrm_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

executable('mmap-test', ['mmap_test.cc'],
           include_directories : incdirs,
           dependencies : [
               bsdrm_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

executable('gbm-test', ['gbmtest.cc'],
           include_directories : incdirs,
           dependencies : [
               bsdrm_dep,
               drmpp_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

executable('dmabuf-test', ['dmabuf_test.cc'],
           include_directories : incdirs,
           dependencies : [
               bsdrm_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

executable('udmabuf-create-test', ['udmabuf_create_test.cc'],
           include_directories : incdirs,
           dependencies : [
               bsdrm_dep,
               math_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

executable('yuv-to-rgb-test', ['yuv_to_rgb_test.cc'],
           include_directories : incdirs,
           dependencies : [
               bsdrm_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

if get_option('vulkan')
    executable('vk-glow', ['vk_glow.cc'],
               include_directories : incdirs,
               dependencies : [
                   bsdrm_dep,
                   vulkan_dep,
                   math_dep,
               ],
               install : true,
               install_dir : get_option('bindir'),
    )
endif