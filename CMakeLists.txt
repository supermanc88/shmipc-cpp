cmake_minimum_required(VERSION 3.16)

project(
    shmipc
    VERSION 0.1.0
    DESCRIPTION "Shared-memory inter-process communication library"
    LANGUAGES CXX
)
include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(SHMIPC_BUILD_TESTS "Build shmipc tests" ${BUILD_TESTING})
option(SHMIPC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(SHMIPC_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(SHMIPC_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(SHMIPC_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(SHMIPC_ENABLE_GO_ORACLE_TESTS "Enable tests against the fixed Go reference" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ShmipcProjectOptions)

add_library(
    shmipc
    src/protocol/control_codec.cpp
    src/shm/buffer_layout.cpp
    src/shm/buffer_io.cpp
    src/shm/buffer_pool.cpp
    src/shm/queue_layout.cpp
    src/shm/shared_queue.cpp
    src/shm/shared_memory_region.cpp
    src/version.cpp
)
add_library(shmipc::shmipc ALIAS shmipc)

target_compile_features(shmipc PUBLIC cxx_std_17)
target_include_directories(
    shmipc
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
set_target_properties(
    shmipc
    PROPERTIES
        CXX_EXTENSIONS OFF
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
)
shmipc_apply_project_options(shmipc)

if(SHMIPC_BUILD_TESTS)
    add_subdirectory(tests)
endif()

install(
    TARGETS shmipc
    EXPORT shmipcTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
    EXPORT shmipcTargets
    FILE shmipcTargets.cmake
    NAMESPACE shmipc::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/shmipc
)

configure_package_config_file(
    cmake/shmipcConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/shmipcConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/shmipc
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/shmipcConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/shmipcConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/shmipcConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/shmipc
)
