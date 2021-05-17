config_includes = [include_directories('.')]
conf = configuration_data()
if sgx
    conf.merge_from(conf_sgx)
    config_includes += include_directories(sgx_driver_include_path)
endif

graphene_config_h = configure_file(
    input: 'graphene-config.h.in',
    output: 'graphene-config.h',
    configuration: conf,
)

config_dep = declare_dependency(
    include_directories: config_includes,
    sources: graphene_config_h,
)
