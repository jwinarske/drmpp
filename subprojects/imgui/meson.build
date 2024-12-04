project('imgui', 'cpp')

imgui_sources = [
    'imgui.cpp',
    'imgui_demo.cpp',
    'imgui_draw.cpp',
    'imgui_tables.cpp',
    'imgui_widgets.cpp',
    'backends/imgui_impl_vulkan.cpp',
]

imgui_dep = declare_dependency(
    sources : imgui_sources,
    include_directories : include_directories('.')
)