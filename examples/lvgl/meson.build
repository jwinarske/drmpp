lvgl_opt_var = cmake.subproject_options()
lvgl_c_flags = '-DEGL_NO_X11 -DLV_CONF_PATH=' + meson.current_source_dir() + '/lv_conf.h'
lvgl_cxx_flags = '-DEGL_NO_X11 -DLV_CONF_PATH=' + meson.current_source_dir() + '/lv_conf.h'
lvgl_opt_var.add_cmake_defines(
    {
        'CMAKE_VERBOSE_MAKEFILE' : true,
        'CMAKE_C_FLAGS' : lvgl_c_flags,
        'CMAKE_CXX_FLAGS' : lvgl_cxx_flags })

lvgl = cmake.subproject('lvgl', options : lvgl_opt_var)
lvgl_dep = lvgl.dependency('lvgl')
lvgl_thorvg = cmake.subproject('lvgl', options : lvgl_opt_var)
lvgl_thorvg_dep = lvgl_thorvg.dependency('lvgl_thorvg')
lvgl_examples = cmake.subproject('lvgl', options : lvgl_opt_var)
lvgl_examples_dep = lvgl_examples.dependency('lvgl_examples')
lvgl_demos = cmake.subproject('lvgl', options : lvgl_opt_var)
lvgl_demos_dep = lvgl_demos.dependency('lvgl_demos')


executable('drm-gbm-lvgl', [
               'drm_lvgl.cc',
           ],
           cpp_args : ['-DEGL_NO_X11'],
           include_directories : [include_directories('.')],
           dependencies : [
               cxxopts_dep,
               drmp_dep,
               lvgl_dep,
               lvgl_thorvg_dep,
               lvgl_demos_dep,
               gles_dep,
               egl_dep,
               dl_dep,
           ],
           install : true,
)
