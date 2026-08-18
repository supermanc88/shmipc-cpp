add_executable(shmipc_version_test version_test.cpp)
target_link_libraries(shmipc_version_test PRIVATE shmipc::shmipc)
shmipc_apply_project_options(shmipc_version_test)

add_test(NAME shmipc.version COMMAND shmipc_version_test)

add_executable(shmipc_control_header_golden_test control_header_golden_test.cpp)
target_link_libraries(shmipc_control_header_golden_test PRIVATE shmipc::shmipc)
target_include_directories(shmipc_control_header_golden_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_control_header_golden_test)
target_compile_definitions(
    shmipc_control_header_golden_test
    PRIVATE
        SHMIPC_CONTROL_HEADER_GOLDEN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/golden/control_headers.txt"
)
add_test(NAME shmipc.control_header_golden COMMAND shmipc_control_header_golden_test)

add_executable(shmipc_protocol_codec_test protocol_codec_test.cpp)
target_link_libraries(shmipc_protocol_codec_test PRIVATE shmipc::shmipc)
target_include_directories(shmipc_protocol_codec_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_protocol_codec_test)
target_compile_definitions(
    shmipc_protocol_codec_test
    PRIVATE
        SHMIPC_SHM_METADATA_GOLDEN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/golden/shm_metadata.txt"
        SHMIPC_FALLBACK_GOLDEN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/golden/fallback_data.txt"
)
add_test(NAME shmipc.protocol_codec COMMAND shmipc_protocol_codec_test)

add_executable(shmipc_queue_layout_test queue_layout_test.cpp)
target_link_libraries(shmipc_queue_layout_test PRIVATE shmipc::shmipc)
target_include_directories(shmipc_queue_layout_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_queue_layout_test)
target_compile_definitions(
    shmipc_queue_layout_test
    PRIVATE
        SHMIPC_QUEUE_LAYOUT_GOLDEN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/golden/queue_layout.txt"
)
add_test(NAME shmipc.queue_layout COMMAND shmipc_queue_layout_test)

add_executable(shmipc_buffer_layout_test buffer_layout_test.cpp)
target_link_libraries(shmipc_buffer_layout_test PRIVATE shmipc::shmipc)
target_include_directories(shmipc_buffer_layout_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_buffer_layout_test)
target_compile_definitions(
    shmipc_buffer_layout_test
    PRIVATE
        SHMIPC_BUFFER_LAYOUT_GOLDEN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/golden/buffer_layout.txt"
)
add_test(NAME shmipc.buffer_layout COMMAND shmipc_buffer_layout_test)

if(SHMIPC_ENABLE_GO_ORACLE_TESTS)
    find_program(SHMIPC_GO_EXECUTABLE go)
    if(NOT SHMIPC_GO_EXECUTABLE)
        message(FATAL_ERROR "SHMIPC_ENABLE_GO_ORACLE_TESTS requires the Go toolchain")
    endif()
    add_test(
        NAME shmipc.go_protocol_oracle
        COMMAND
            ${SHMIPC_GO_EXECUTABLE} run
            ${PROJECT_SOURCE_DIR}/tools/go_oracle/run_control_header_oracle.go
    )
    set_tests_properties(shmipc.go_protocol_oracle PROPERTIES LABELS oracle)
endif()
