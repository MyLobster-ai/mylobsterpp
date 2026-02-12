include(FetchContent)

# Boost (Beast, Asio, JSON)
set(BOOST_INCLUDE_LIBRARIES beast asio json url)
FetchContent_Declare(
    Boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.87.0/boost-1.87.0-cmake.tar.gz
    URL_HASH SHA256=78fbf579e3caf0f47517d3fb4d9301852c3154bfecdc5eeebd9b2b0292366f5b
    EXCLUDE_FROM_ALL
)
FetchContent_MakeAvailable(Boost)

# nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# spdlog (includes fmt)
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(spdlog)

# CLI11
FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(CLI11)

# SQLiteCpp
FetchContent_Declare(
    SQLiteCpp
    GIT_REPOSITORY https://github.com/SRombauts/SQLiteCpp.git
    GIT_TAG 3.3.2
    GIT_SHALLOW TRUE
)
set(SQLITECPP_RUN_CPPLINT OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SQLiteCpp)

# jwt-cpp
FetchContent_Declare(
    jwt-cpp
    GIT_REPOSITORY https://github.com/Thalhammer/jwt-cpp.git
    GIT_TAG v0.7.0
    GIT_SHALLOW TRUE
)
set(JWT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(JWT_DISABLE_PICOJSON ON CACHE BOOL "Disable picojson, use nlohmann/json instead" FORCE)
FetchContent_MakeAvailable(jwt-cpp)

# cpp-httplib (header-only)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.18.3
    GIT_SHALLOW TRUE
)
set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)

# yaml-cpp
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG yaml-cpp-0.9.0
    GIT_SHALLOW TRUE
)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(yaml-cpp)

# stduuid (header-only)
FetchContent_Declare(
    stduuid
    GIT_REPOSITORY https://github.com/mariusbancila/stduuid.git
    GIT_TAG v1.2.3
    GIT_SHALLOW TRUE
)
set(UUID_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(stduuid)

# Catch2 (test framework, only if building tests)
if(MYLOBSTER_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.7.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
endif()

# System dependencies
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)
