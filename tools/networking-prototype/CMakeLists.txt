cmake_minimum_required(VERSION 3.15)
project(ValDroidNetworkingPrototype LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(GNS_SOURCE_DIR "" CACHE PATH "Pinned official GameNetworkingSockets checkout")
if(NOT EXISTS "${GNS_SOURCE_DIR}/include/steam/steamnetworkingsockets.h")
    message(FATAL_ERROR "Set GNS_SOURCE_DIR to the pinned official GameNetworkingSockets checkout")
endif()
find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${GNS_SOURCE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE GNS_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE GNS_GIT_RESULT)
if(NOT GNS_GIT_RESULT EQUAL 0 OR NOT GNS_REVISION STREQUAL "d534c19aa760df3fb75fd20db13ba1932b8a5463")
    message(FATAL_ERROR "GNS checkout must be pinned to d534c19aa760df3fb75fd20db13ba1932b8a5463")
endif()

# Minimal official transport build. Do not change certificate trust/auth settings.
set(BUILD_SHARED_LIB ON CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIB OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ENABLE_ICE OFF CACHE BOOL "" FORCE)
set(USE_STEAMWEBRTC OFF CACHE BOOL "" FORCE)
add_subdirectory("${GNS_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/gns")

add_library(valdroid_gns_adapter STATIC gns_transport.cpp gns_native.cpp)
target_link_libraries(valdroid_gns_adapter PUBLIC GameNetworkingSockets::shared)
target_include_directories(valdroid_gns_adapter PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_options(valdroid_gns_adapter PRIVATE -Wall -Wextra -Werror)

add_executable(valdroid_gns_smoke linked_smoke.cpp)
target_link_libraries(valdroid_gns_smoke PRIVATE valdroid_gns_adapter)
target_compile_options(valdroid_gns_smoke PRIVATE -Wall -Wextra -Werror)

add_executable(valdroid_gns_recording_tests test_transport.cpp)
target_link_libraries(valdroid_gns_recording_tests PRIVATE valdroid_gns_adapter)
target_compile_options(valdroid_gns_recording_tests PRIVATE -Wall -Wextra -Werror -UNDEBUG)

enable_testing()
add_test(NAME recording_fake_control_flow COMMAND valdroid_gns_recording_tests)
add_test(NAME real_gns_local_smoke COMMAND valdroid_gns_smoke)
set_tests_properties(real_gns_local_smoke PROPERTIES TIMEOUT 20)
