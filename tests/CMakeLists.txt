# Existing per-subsystem tests live alongside this file (agent/, channels/,
# core/, gateway/, infra/, memory/, providers/, ...). They are always built
# into the `mylobster_tests` executable and discovered case-by-case via
# catch_discover_tests.
#
# v4_29/ contains ~4.9k auto-generated parity-mirror tests against OpenClaw
# v2026.4.29 (one TEST_CASE per upstream `it()` block; ~42k cases total).
# They build into a separate `mylobster_v4_29_parity_tests` executable, gated
# behind MYLOBSTER_BUILD_V4_29_PARITY (default OFF).
#
# v5_2/ contains the same shape of mirrors against the next pinned upstream
# tag (OpenClaw v2026.5.2). It builds into `mylobster_v5_2_parity_tests`
# and is gated behind MYLOBSTER_BUILD_V5_2_PARITY (default OFF). The v4_29
# tree is kept around as a frozen baseline so the version-bump delta stays
# auditable.
#
# Each parity binary registers one CTest entry that runs the whole binary
# (instead of 42k individual entries; catch_discover_tests would be too slow
# for that scale).

include(CTest)
include(Catch)

# Existing tests: glob everything outside the parity mirror trees.
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

# Helper: register a parity-mirror executable for an upstream tag dir.
function(mylobster_register_parity_target tag_label tag_dir)
    string(REPLACE "." "_" target_suffix "${tag_label}")
    set(parity_target "mylobster_${tag_dir}_parity_tests")
    set(parity_label  "${tag_dir}")

    file(GLOB_RECURSE parity_sources
        LIST_DIRECTORIES false
        "${CMAKE_CURRENT_SOURCE_DIR}/${tag_dir}/*.cpp"
    )
    list(LENGTH parity_sources parity_count)
    message(STATUS "${tag_dir} parity mirrors: ${parity_count} test files")

    add_executable(${parity_target} ${parity_sources})
    target_link_libraries(${parity_target} PRIVATE
        mylobster_lib
        Catch2::Catch2WithMain
    )
    target_include_directories(${parity_target} PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/${tag_dir}"
    )

    add_test(
        NAME ${parity_label}_parity_full
        COMMAND ${parity_target} "[${parity_label}]" --reporter compact
    )
    set_tests_properties(${parity_label}_parity_full PROPERTIES
        LABELS "${parity_label};parity"
    )

    foreach(tag IN ITEMS anthropic openai gemini bedrock providers agents
                          infra gateway channels plugins memory)
        add_test(
            NAME "${parity_label}_parity_${tag}"
            COMMAND ${parity_target} "[${parity_label}][${tag}]" --reporter compact
        )
        set_tests_properties("${parity_label}_parity_${tag}" PROPERTIES
            LABELS "${parity_label};parity;${tag}"
        )
    endforeach()
endfunction()

if(MYLOBSTER_BUILD_V4_29_PARITY)
    mylobster_register_parity_target("v2026.4.29" "v4_29")
endif()

if(MYLOBSTER_BUILD_V5_2_PARITY)
    mylobster_register_parity_target("v2026.5.2" "v5_2")
endif()
