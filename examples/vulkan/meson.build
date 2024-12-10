executable('vk-khr-inp', [
               'vk_khr_inp.cc',
           ],
           dependencies : [
               cxxopts_dep,
               drmpp_dep,
               vulkan_dep,
               vulkan_headers_dep,
               dl_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)

executable('vk-kms-inp', [
               'vk_kms_inp.cc',
           ],
           dependencies : [
               cxxopts_dep,
               drmpp_dep,
               vulkan_dep,
               vulkan_headers_dep,
               dl_dep,
           ],
           install : true,
           install_dir : get_option('bindir'),
)
