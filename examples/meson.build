cmake = import('cmake')

opt_var = cmake.subproject_options()
opt_var.add_cmake_defines({ 'CMAKE_VERBOSE_MAKEFILE' : true })
opt_var.add_cmake_defines({ 'CMAKE_POLICY_DEFAULT_CMP0156' : 'NEW'})
opt_var.add_cmake_defines({ 'CMAKE_POLICY_DEFAULT_CMP0159' : 'NEW'})
cxxopts = cmake.subproject('cxxopts-3.2.1', options : opt_var)
cxxopts_dep = cxxopts.dependency('cxxopts')


executable('drm-json', ['drm_json.cc'],
           include_directories : incdirs,
           dependencies : [
               cxxopts_dep,
               drmp_dep,
               rapidjson_dep,
           ],
           install : true,
)

executable('drm-input', ['drm_input.cc'],
           include_directories : incdirs,
           dependencies : [
               cxxopts_dep,
               di_dep,
               drmp_dep,
               rapidjson_dep,
           ],
           install : true,
)