# Collect all test sources
file(GLOB_RECURSE TEST_SOURCES "*.cpp")

add_executable(openclaw_tests ${TEST_SOURCES})
target_link_libraries(openclaw_tests PRIVATE
    openclaw_lib
    Catch2::Catch2WithMain
)

include(CTest)
include(Catch)
catch_discover_tests(openclaw_tests)
