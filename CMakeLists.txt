cmake_minimum_required(VERSION 3.25)
project(mylobsterpp
    VERSION 2026.2.26
    DESCRIPTION "MyLobster++ C++23 AI Assistant Platform"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(MYLOBSTER_BUILD_TESTS "Build test suite" ON)
option(MYLOBSTER_ENABLE_SANITIZERS "Enable address/UB sanitizers" OFF)
option(MYLOBSTER_BUILD_SHARED "Build shared library instead of static" OFF)
option(MYLOBSTER_BUILD_EXECUTABLE "Build the CLI executable" ON)

# Compiler warnings
if(MSVC)
    add_compile_definitions(WIN32_LEAN_AND_MEAN NOMINMAX)
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

if(MYLOBSTER_ENABLE_SANITIZERS AND NOT MSVC)
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

# Library target
if(MYLOBSTER_BUILD_SHARED)
    add_library(mylobster_lib SHARED ${LIB_SOURCES})
    set_target_properties(mylobster_lib PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        POSITION_INDEPENDENT_CODE ON
    )
    target_compile_definitions(mylobster_lib
        PUBLIC MYLOBSTER_SHARED
        PRIVATE MYLOBSTER_BUILDING
    )
    if(MSVC)
        set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
    endif()
else()
    add_library(mylobster_lib STATIC ${LIB_SOURCES})
endif()

add_library(mylobster::mylobster ALIAS mylobster_lib)

target_include_directories(mylobster_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(mylobster_lib PUBLIC
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
    $<$<PLATFORM_ID:Windows>:ws2_32>
)

# Main executable
if(MYLOBSTER_BUILD_EXECUTABLE)
    add_executable(mylobsterpp src/main.cpp)
    target_link_libraries(mylobsterpp PRIVATE mylobster_lib)
endif()

# Tests
if(MYLOBSTER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS mylobster_lib
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(MYLOBSTER_BUILD_EXECUTABLE)
    install(TARGETS mylobsterpp RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(DIRECTORY include/openclaw DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_package_config_file(
    cmake/mylobsterConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/mylobsterConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mylobster
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/mylobsterConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/mylobsterConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/mylobsterConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mylobster
)
