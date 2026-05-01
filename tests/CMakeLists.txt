# Existing per-subsystem tests live alongside this file (agent/, channels/,
# core/, gateway/, infra/, memory/, providers/, ...). They are always built
# into the `mylobster_tests` executable and discovered case-by-case via
# catch_discover_tests.
#
# v4_29/ contains ~4.9k auto-generated parity-mirror tests against OpenClaw
# v2026.4.29 (one TEST_CASE per upstream `it()` block; ~42k cases total).
# They build into a separate `mylobster_v4_29_parity_tests` executable, gated
# behind MYLOBSTER_BUILD_V4_29_PARITY (default OFF). One CTest entry runs the
# whole binary instead of registering 42k individual entries.

include(CTest)
include(Catch)

# Existing tests: glob everything outside v4_29/.
file(GLOB_RECURSE TEST_SOURCES
    LIST_DIRECTORIES false
    "${CMAKE_CURRENT_SOURCE_DIR}/agent/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/channels/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/cli/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/context_engine/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/cron/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/gateway/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/infra/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/memory/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/providers/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/routing/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/sessions/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tasks/*.cpp"
)

add_executable(mylobster_tests ${TEST_SOURCES})
target_link_libraries(mylobster_tests PRIVATE
    mylobster_lib
    Catch2::Catch2WithMain
)
catch_discover_tests(mylobster_tests)

if(MYLOBSTER_BUILD_V4_29_PARITY)
    file(GLOB_RECURSE V4_29_TEST_SOURCES
        LIST_DIRECTORIES false
        "${CMAKE_CURRENT_SOURCE_DIR}/v4_29/*.cpp"
    )
    list(LENGTH V4_29_TEST_SOURCES V4_29_COUNT)
    message(STATUS "v4_29 parity mirrors: ${V4_29_COUNT} test files")

    add_executable(mylobster_v4_29_parity_tests ${V4_29_TEST_SOURCES})
    target_link_libraries(mylobster_v4_29_parity_tests PRIVATE
        mylobster_lib
        Catch2::Catch2WithMain
    )
    target_include_directories(mylobster_v4_29_parity_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/v4_29"
    )

    # Single CTest entry that runs the whole v4.29 parity binary. Avoids
    # registering 42k individual ctest entries (catch_discover_tests would
    # be too slow for that scale).
    add_test(
        NAME v4_29_parity_full
        COMMAND mylobster_v4_29_parity_tests "[v4_29]" --reporter compact
    )
    set_tests_properties(v4_29_parity_full PROPERTIES
        LABELS "v4_29;parity"
    )

    # Per-tag entries for narrowing during iteration on a single subsystem.
    foreach(tag IN ITEMS anthropic openai gemini bedrock providers agents
                          infra gateway channels plugins memory)
        add_test(
            NAME "v4_29_parity_${tag}"
            COMMAND mylobster_v4_29_parity_tests "[v4_29][${tag}]" --reporter compact
        )
        set_tests_properties("v4_29_parity_${tag}" PROPERTIES
            LABELS "v4_29;parity;${tag}"
        )
    endforeach()
endif()
