# Source identity, presumed locations and content digests.
#
# A leaf library: every component that needs to talk about where something came
# from uses this one, so no downstream component invents its own location or
# identity scheme (ARCHITECTURE.md 7, 13).
add_library(cppl_source STATIC
    src/digest.cpp
    src/location.cpp)

target_include_directories(cppl_source PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(cppl_source PRIVATE cppl_project_options)

add_library(cppl::source ALIAS cppl_source)
