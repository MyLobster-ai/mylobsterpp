# Collect all test sources
file(GLOB_RECURSE TEST_SOURCES "*.cpp")

add_executable(mylobster_tests ${TEST_SOURCES})
target_link_libraries(mylobster_tests PRIVATE
    mylobster_lib
    Catch2::Catch2WithMain
)

include(CTest)
include(Catch)
catch_discover_tests(mylobster_tests)
