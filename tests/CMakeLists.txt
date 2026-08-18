add_executable(shmipc_version_test version_test.cpp)
target_link_libraries(shmipc_version_test PRIVATE shmipc::shmipc)
shmipc_apply_project_options(shmipc_version_test)

add_test(NAME shmipc.version COMMAND shmipc_version_test)
