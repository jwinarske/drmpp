executable('drm-snake', ['drm_snake.cc', 'snake.cc'],
           cpp_args : ['-DEGL_NO_X11'],
           dependencies : [
               cxxopts_dep,
               egl_dep,
               opengl_dep,
               drmp_dep,
               gbm_dep,
           ],
           install : false,
)
