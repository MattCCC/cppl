add_executable(cppl-format
    main.cpp
)

target_link_libraries(cppl-format PRIVATE
    cppl_formatter
    cppl_project_options
)

target_compile_features(cppl-format PRIVATE cxx_std_20)
