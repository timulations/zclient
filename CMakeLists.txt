cmake_minimum_required(VERSION 3.16)

project(zclient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

if (UNIX AND NOT APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcoroutines")
elseif(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++ -fcoroutines-ts")
endif()

# Dependencies
find_package(Boost REQUIRED COMPONENTS system coroutine program_options)
find_package(OpenSSL REQUIRED)

# Headers
include_directories(
    include
    zlogger/include
    ${Boost_INCLUDE_DIR}
    ${OPENSSL_INCLUDE_DIR}
    ${JSONCPP_INCLUDE_DIR}
)

add_subdirectory(certify)

# Sources
set(SOURCES
    src/http_client.cpp
    src/websocket_client.cpp
)

# Zsocket library
add_library(
    libzclient
    ${SOURCES}
)

if (WIN32 AND MSVC)
    target_compile_options(libzclient PRIVATE "/bigobj")
endif()

target_link_libraries(
    libzclient
    PUBLIC
    Boost::system
    Boost::coroutine
    Boost::program_options
    ${OPENSSL_LIBRARIES}
    ${JSONCPP_LIB_DIR}
    certify::core
)

# CLI
add_executable(
    zclient_cli
    frontend/zclient_cli.cpp
)

target_link_libraries(
    zclient_cli
    PUBLIC
    libzclient
)


#  Examples
add_executable(
    await_sequential_http_requests
    examples/await_sequential_http_requests.cpp
)

target_link_libraries(
    await_sequential_http_requests
    PUBLIC
    libzclient
)

add_executable(
    callback_sequential_http_requests
    examples/callback_sequential_http_requests.cpp
)

target_link_libraries(
    callback_sequential_http_requests
    PUBLIC
    libzclient
)

add_executable(
    await_parallel_http_requests
    examples/await_parallel_http_requests.cpp
)

target_link_libraries(
    await_parallel_http_requests
    PUBLIC
    libzclient
)

add_executable(
    multithreaded_await_parallel_http_requests
    examples/multithreaded_await_parallel_http_requests.cpp
)

target_link_libraries(
    multithreaded_await_parallel_http_requests
    PUBLIC
    libzclient
)

add_executable(
    callback_parallel_http_requests
    examples/callback_parallel_http_requests.cpp
)

target_link_libraries(
    callback_parallel_http_requests
    PUBLIC
    libzclient
)

# Tests
set(JSONCPP_WITH_TESTS OFF CACHE BOOL "Enable tests for jsoncpp_lib" FORCE) # disable jsoncpp tests
add_subdirectory(jsoncpp)

# remove submodule tests from CTest suite:
configure_file(${CMAKE_SOURCE_DIR}/CTestCustom.cmake ${CMAKE_BINARY_DIR} @ONLY)

add_executable(
    test_http_client
    test/test_http_client.cpp
)

target_include_directories(
    test_http_client
    PRIVATE
    jsoncpp/include
)

target_link_libraries(
    test_http_client
    PRIVATE
    libzclient
    jsoncpp_static
)

enable_testing()

# start the mock server
set(MOCK_SERVER_UNSECURED_PORT 3001)
set(MOCK_SERVER_SECURED_PORT 443)

target_compile_definitions(
    test_http_client
    PRIVATE
    MOCK_SERVER_UNSECURED_PORT="${MOCK_SERVER_UNSECURED_PORT}"
    MOCK_SERVER_SECURED_PORT="${MOCK_SERVER_SECURED_PORT}"
)

add_custom_target(test_http_client_against_mock_server
    COMMAND openssl req -x509 -nodes -days 1 -newkey rsa:2048 -keyout ${CMAKE_CURRENT_BINARY_DIR}/server.key -out ${CMAKE_CURRENT_BINARY_DIR}/server.crt -config ${CMAKE_CURRENT_SOURCE_DIR}/test/mock_server/mock_server_config.cnf
    COMMAND bash -c "${CMAKE_CURRENT_SOURCE_DIR}/test/mock_server/launch_mock_server.sh ${CMAKE_CURRENT_BINARY_DIR}/server.key ${CMAKE_CURRENT_BINARY_DIR}/server.crt ${MOCK_SERVER_UNSECURED_PORT} ${MOCK_SERVER_SECURED_PORT}"
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target test_http_client
    COMMAND test_http_client ${CMAKE_CURRENT_SOURCE_DIR}/test/mock_server/test_endpoint_config.json
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/test/mock_server/kill_mock_server.cmake
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_test(
    NAME test_http_client
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target test_http_client_against_mock_server
)
