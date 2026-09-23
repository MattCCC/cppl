add_executable(cppl-lsp
    main.cpp
)

target_link_libraries(cppl-lsp PRIVATE
    cppl_lsp
    cppl_project_options
)

target_compile_features(cppl-lsp PRIVATE cxx_std_20)
