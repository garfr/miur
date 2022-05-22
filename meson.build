project('miur', 'c')

cmake = import('cmake')

src = [
    'src/main.c',
    'src/render.c',
    'src/log.c',
    'src/membuf.c',
    'src/gltf.c',
    'src/bsl.c',
    'src/device.c',
    'src/swapchain.c',
    'src/string.c',
    'src/shader.c',
    'src/material.c',
    'src/render_graph.c',
    'src/json.c',
    'src/thread.c',
    'src/fs_monitor.c',
]

warning_level = 3

cc = meson.get_compiler('c')

deps_path = meson.current_source_dir() / 'deps'

deps_path = meson.current_source_dir() / 'deps'
deps_inc = include_directories(deps_path)

shaderc_inc = include_directories(deps_path / 'shaderc/include/')
shaderc_dep = cc.find_library('shaderc_combined', dirs : [deps_path / 'shaderc/lib'])

cwin = subproject('cwin').get_variable('cwin_dep')
bsl = subproject('bsl').get_variable('bsl_dep')

deps = [cwin, bsl, shaderc_dep]
cdata = configuration_data()
cdata.set('GPU_VULKAN_SUPPORT', true)

vulkan = dependency('vulkan')
deps += vulkan
configure_file(output : 'gpu_config.h',
                     configuration : cdata)

conf = include_directories('.')
inc = include_directories('include')

executable('miur',
           src,
           include_directories : [conf, inc, deps_inc],
           dependencies : deps)
