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

    'deps/cJSON.c',
]

warning_level = 3
deps_path = meson.current_source_dir() / 'deps'

deps_path = meson.current_source_dir() / 'deps'
deps_inc = include_directories(deps_path)

cglm_path = meson.current_source_dir() / 'deps/cglm/include'
cglm_inc = include_directories(cglm_path)

cwin = subproject('cwin').get_variable('cwin_dep')

deps = [cwin]
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
           include_directories : [conf, inc, deps_inc, cglm_inc],
           dependencies : deps)