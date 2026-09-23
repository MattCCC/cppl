# One object library per component, whose sources are the component's own
# headers (cmake/HeaderChecks.cmake). Nothing builds them; they exist so that
# the compilation database compiles each header on its own, with its owner's
# include paths, definitions and dependencies, and `lint` analyzes it as a main
# file. Marking the headers as C++ happens in this directory only, so no
# component that lists a header among its own sources compiles it.

get_property(targets GLOBAL PROPERTY CPPL_HEADER_CHECK_TARGETS)

foreach(target IN LISTS targets)
    get_property(headers GLOBAL PROPERTY "CPPL_HEADERS_${target}")

    set_source_files_properties(${headers} PROPERTIES LANGUAGE CXX)

    set(checks "${target}_headers")
    add_library(${checks} OBJECT EXCLUDE_FROM_ALL ${headers})

    # The owner's usage requirements, and its private ones too: a header in a
    # component's own source directory may reach for a private dependency.
    target_link_libraries(
        ${checks}
        PRIVATE
            ${target}
            $<TARGET_PROPERTY:${target},LINK_LIBRARIES>
            cppl_project_options
    )
    target_include_directories(
        ${checks}
        PRIVATE
            $<TARGET_PROPERTY:${target},INCLUDE_DIRECTORIES>
    )
    target_compile_definitions(
        ${checks}
        PRIVATE
            $<TARGET_PROPERTY:${target},COMPILE_DEFINITIONS>
    )
endforeach()
