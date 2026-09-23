add_executable(cppl-spec-rules
    main.cpp
)

target_link_libraries(cppl-spec-rules PRIVATE cppl_project_options)

target_compile_features(cppl-spec-rules PRIVATE cxx_std_20)
