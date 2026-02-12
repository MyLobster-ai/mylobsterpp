cmake_minimum_required(VERSION 3.25)
project(mylobsterpp
    VERSION 0.1.0
    DESCRIPTION "MyLobster++ C++23 AI Assistant Platform"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(OPENCLAW_BUILD_TESTS "Build test suite" ON)
option(OPENCLAW_ENABLE_SANITIZERS "Enable address/UB sanitizers" OFF)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /wd4100 /EHsc)
    add_compile_options($<$<CONFIG:Debug>:/Od> $<$<CONFIG:Release>:/O2>)
else()
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:Debug>:-O0>
        $<$<CONFIG:Release>:-O2>
    )
endif()

if(OPENCLAW_ENABLE_SANITIZERS AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

# Dependencies
include(cmake/Dependencies.cmake)

# Library sources
file(GLOB_RECURSE LIB_SOURCES
    src/core/*.cpp
    src/gateway/*.cpp
    src/channels/*.cpp
    src/agent/*.cpp
    src/providers/*.cpp
    src/memory/*.cpp
    src/browser/*.cpp
    src/sessions/*.cpp
    src/plugins/*.cpp
    src/cli/*.cpp
    src/routing/*.cpp
    src/cron/*.cpp
    src/infra/*.cpp
)

add_library(openclaw_lib STATIC ${LIB_SOURCES})
target_include_directories(openclaw_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(openclaw_lib PUBLIC
    Boost::beast
    Boost::asio
    Boost::json
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    CLI11::CLI11
    SQLiteCpp
    jwt-cpp::jwt-cpp
    httplib::httplib
    yaml-cpp::yaml-cpp
    stduuid
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
)

# Main executable
add_executable(mylobsterpp src/main.cpp)
target_link_libraries(mylobsterpp PRIVATE openclaw_lib)

# Tests
if(OPENCLAW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install
install(TARGETS mylobsterpp RUNTIME DESTINATION bin)
install(DIRECTORY include/openclaw DESTINATION include)
