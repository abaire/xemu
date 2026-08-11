if host_os == 'windows' and get_option('superluminal')
    superluminal = declare_dependency(include_directories : ['./include'],
                                      compile_args : '-DXEMU_SUPERLUMINAL')
else
    superluminal = declare_dependency()
endif
