cmake_minimum_required(VERSION 3.16)
project(shmipc_package_consumer LANGUAGES CXX)

find_package(shmipc CONFIG REQUIRED)

add_executable(shmipc_package_consumer main.cpp)
target_link_libraries(shmipc_package_consumer PRIVATE shmipc::shmipc)
