find_package(Threads REQUIRED)

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

add_executable(shmipc_shared_queue_test shared_queue_test.cpp)
target_link_libraries(shmipc_shared_queue_test PRIVATE shmipc::shmipc Threads::Threads)
target_include_directories(shmipc_shared_queue_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_shared_queue_test)
add_test(NAME shmipc.shared_queue COMMAND shmipc_shared_queue_test)

add_executable(shmipc_buffer_layout_test buffer_layout_test.cpp)
target_link_libraries(shmipc_buffer_layout_test PRIVATE shmipc::shmipc)
target_include_directories(shmipc_buffer_layout_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_buffer_layout_test)
target_compile_definitions(
    shmipc_buffer_layout_test
    PRIVATE
        SHMIPC_BUFFER_LAYOUT_GOLDEN_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/golden/buffer_layout.txt"
        SHMIPC_LAYOUT_CORRUPTION_CORPUS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/corpus/layout_corruption.txt"
)
add_test(NAME shmipc.buffer_layout COMMAND shmipc_buffer_layout_test)

add_executable(shmipc_shared_memory_region_test shared_memory_region_test.cpp)
target_link_libraries(shmipc_shared_memory_region_test PRIVATE shmipc::shmipc)
target_include_directories(shmipc_shared_memory_region_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_shared_memory_region_test)
add_test(NAME shmipc.shared_memory_region COMMAND shmipc_shared_memory_region_test)

add_executable(shmipc_buffer_pool_test buffer_pool_test.cpp)
target_link_libraries(shmipc_buffer_pool_test PRIVATE shmipc::shmipc)
target_include_directories(shmipc_buffer_pool_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_buffer_pool_test)
add_test(NAME shmipc.buffer_pool COMMAND shmipc_buffer_pool_test)

add_executable(shmipc_buffer_io_test buffer_io_test.cpp)
target_link_libraries(shmipc_buffer_io_test PRIVATE shmipc::shmipc)
target_include_directories(shmipc_buffer_io_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_buffer_io_test)
add_test(NAME shmipc.buffer_io COMMAND shmipc_buffer_io_test)

add_executable(shmipc_control_socket_test control_socket_test.cpp)
target_link_libraries(shmipc_control_socket_test PRIVATE shmipc::shmipc Threads::Threads)
target_include_directories(shmipc_control_socket_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_control_socket_test)
add_test(NAME shmipc.control_socket COMMAND shmipc_control_socket_test)

add_executable(shmipc_epoll_dispatcher_test epoll_dispatcher_test.cpp)
target_link_libraries(shmipc_epoll_dispatcher_test PRIVATE shmipc::shmipc Threads::Threads)
target_include_directories(shmipc_epoll_dispatcher_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_epoll_dispatcher_test)
add_test(NAME shmipc.epoll_dispatcher COMMAND shmipc_epoll_dispatcher_test)

add_executable(shmipc_v2_handshake_test v2_handshake_test.cpp)
target_link_libraries(shmipc_v2_handshake_test PRIVATE shmipc::shmipc Threads::Threads)
target_include_directories(shmipc_v2_handshake_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_v2_handshake_test)
add_test(NAME shmipc.v2_handshake COMMAND shmipc_v2_handshake_test)

add_executable(shmipc_protocol_version_negotiation_test
               protocol_version_negotiation_test.cpp)
target_link_libraries(shmipc_protocol_version_negotiation_test
                      PRIVATE shmipc::shmipc Threads::Threads)
target_include_directories(shmipc_protocol_version_negotiation_test
                           PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_protocol_version_negotiation_test)
add_test(NAME shmipc.protocol_version_negotiation
         COMMAND shmipc_protocol_version_negotiation_test)

add_executable(shmipc_v3_handshake_test v3_handshake_test.cpp)
target_link_libraries(shmipc_v3_handshake_test
                      PRIVATE shmipc::shmipc Threads::Threads)
target_include_directories(shmipc_v3_handshake_test
                           PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_v3_handshake_test)
add_test(NAME shmipc.v3_handshake COMMAND shmipc_v3_handshake_test)

add_executable(shmipc_v2_client_session_test v2_client_session_test.cpp)
target_link_libraries(shmipc_v2_client_session_test PRIVATE shmipc::shmipc Threads::Threads)
target_include_directories(shmipc_v2_client_session_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_v2_client_session_test)
add_test(NAME shmipc.v2_client_session COMMAND shmipc_v2_client_session_test)

add_executable(shmipc_v2_server_session_test v2_server_session_test.cpp)
target_link_libraries(shmipc_v2_server_session_test PRIVATE shmipc::shmipc Threads::Threads)
target_include_directories(shmipc_v2_server_session_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_v2_server_session_test)
add_test(NAME shmipc.v2_server_session COMMAND shmipc_v2_server_session_test)

add_executable(shmipc_v2_multiplexed_session_test v2_multiplexed_session_test.cpp)
target_link_libraries(shmipc_v2_multiplexed_session_test PRIVATE shmipc::shmipc Threads::Threads)
target_include_directories(shmipc_v2_multiplexed_session_test PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_v2_multiplexed_session_test)
add_test(NAME shmipc.v2_multiplexed_session COMMAND shmipc_v2_multiplexed_session_test)

add_executable(shmipc_v3_multiplexed_session_test
               v3_multiplexed_session_test.cpp)
target_link_libraries(shmipc_v3_multiplexed_session_test
                      PRIVATE shmipc::shmipc Threads::Threads)
target_include_directories(shmipc_v3_multiplexed_session_test
                           PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_v3_multiplexed_session_test)
add_test(NAME shmipc.v3_multiplexed_session
         COMMAND shmipc_v3_multiplexed_session_test)

add_executable(shmipc_buffer_pool_interop_helper buffer_pool_interop_helper.cpp)
target_link_libraries(shmipc_buffer_pool_interop_helper PRIVATE shmipc::shmipc)
target_include_directories(shmipc_buffer_pool_interop_helper PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_buffer_pool_interop_helper)

add_executable(shmipc_shared_queue_interop_helper shared_queue_interop_helper.cpp)
target_link_libraries(shmipc_shared_queue_interop_helper PRIVATE shmipc::shmipc)
target_include_directories(shmipc_shared_queue_interop_helper PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_shared_queue_interop_helper)

add_executable(shmipc_v2_handshake_interop_helper v2_handshake_interop_helper.cpp)
target_link_libraries(shmipc_v2_handshake_interop_helper PRIVATE shmipc::shmipc Threads::Threads)
target_include_directories(shmipc_v2_handshake_interop_helper PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_v2_handshake_interop_helper)

add_executable(shmipc_protocol_version_negotiation_interop_helper
               protocol_version_negotiation_interop_helper.cpp)
target_link_libraries(shmipc_protocol_version_negotiation_interop_helper
                      PRIVATE shmipc::shmipc)
target_include_directories(
    shmipc_protocol_version_negotiation_interop_helper
    PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(
    shmipc_protocol_version_negotiation_interop_helper)

add_executable(shmipc_v3_handshake_interop_helper
               v3_handshake_interop_helper.cpp)
target_link_libraries(shmipc_v3_handshake_interop_helper
                      PRIVATE shmipc::shmipc Threads::Threads)
target_include_directories(shmipc_v3_handshake_interop_helper
                           PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_v3_handshake_interop_helper)

add_executable(shmipc_v2_client_session_interop_helper v2_client_session_interop_helper.cpp)
target_link_libraries(shmipc_v2_client_session_interop_helper PRIVATE shmipc::shmipc Threads::Threads)
target_include_directories(shmipc_v2_client_session_interop_helper PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_v2_client_session_interop_helper)

add_executable(shmipc_v2_server_session_interop_helper v2_server_session_interop_helper.cpp)
target_link_libraries(shmipc_v2_server_session_interop_helper PRIVATE shmipc::shmipc Threads::Threads)
target_include_directories(shmipc_v2_server_session_interop_helper PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_v2_server_session_interop_helper)

add_executable(shmipc_v2_multiplexed_session_interop_helper v2_multiplexed_session_interop_helper.cpp)
target_link_libraries(shmipc_v2_multiplexed_session_interop_helper PRIVATE shmipc::shmipc Threads::Threads)
target_include_directories(shmipc_v2_multiplexed_session_interop_helper PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_v2_multiplexed_session_interop_helper)

add_executable(shmipc_v3_multiplexed_session_interop_helper
               v3_multiplexed_session_interop_helper.cpp)
target_link_libraries(shmipc_v3_multiplexed_session_interop_helper
                      PRIVATE shmipc::shmipc Threads::Threads)
target_include_directories(shmipc_v3_multiplexed_session_interop_helper
                           PRIVATE ${PROJECT_SOURCE_DIR}/src)
shmipc_apply_project_options(shmipc_v3_multiplexed_session_interop_helper)

if(SHMIPC_ENABLE_GO_ORACLE_TESTS)
    find_program(SHMIPC_GO_EXECUTABLE go)
    if(NOT SHMIPC_GO_EXECUTABLE)
        message(FATAL_ERROR "SHMIPC_ENABLE_GO_ORACLE_TESTS requires the Go toolchain")
    endif()
    add_test(
        NAME shmipc.go_protocol_oracle
        COMMAND
            ${CMAKE_COMMAND} -E env
            SHMIPC_CPP_BUFFER_POOL_HELPER=$<TARGET_FILE:shmipc_buffer_pool_interop_helper>
            SHMIPC_CPP_QUEUE_HELPER=$<TARGET_FILE:shmipc_shared_queue_interop_helper>
            SHMIPC_CPP_V2_HANDSHAKE_HELPER=$<TARGET_FILE:shmipc_v2_handshake_interop_helper>
            SHMIPC_CPP_PROTOCOL_VERSION_NEGOTIATION_HELPER=$<TARGET_FILE:shmipc_protocol_version_negotiation_interop_helper>
            SHMIPC_CPP_V3_HANDSHAKE_HELPER=$<TARGET_FILE:shmipc_v3_handshake_interop_helper>
            SHMIPC_CPP_V2_CLIENT_SESSION_HELPER=$<TARGET_FILE:shmipc_v2_client_session_interop_helper>
            SHMIPC_CPP_V2_SERVER_SESSION_HELPER=$<TARGET_FILE:shmipc_v2_server_session_interop_helper>
            SHMIPC_CPP_V2_MULTIPLEXED_SESSION_HELPER=$<TARGET_FILE:shmipc_v2_multiplexed_session_interop_helper>
            SHMIPC_CPP_V3_MULTIPLEXED_SESSION_HELPER=$<TARGET_FILE:shmipc_v3_multiplexed_session_interop_helper>
            ${SHMIPC_GO_EXECUTABLE} run
            ${PROJECT_SOURCE_DIR}/tools/go_oracle/run_control_header_oracle.go
    )
    set_tests_properties(shmipc.go_protocol_oracle PROPERTIES LABELS oracle)
endif()
