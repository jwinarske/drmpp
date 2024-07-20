cmake = import('cmake')

opt_var = cmake.subproject_options()
opt_var.add_cmake_defines({ 'CMAKE_VERBOSE_MAKEFILE' : true })
cxxopts = cmake.subproject('cxxopts-3.2.1', options : opt_var)
cxxopts_dep = cxxopts.dependency('cxxopts')

executable('drm-json', ['drm_json.cc'],
           include_directories : incdirs,
           dependencies : [
               cxxopts_dep,
               drmp_dep,
           ],
)
