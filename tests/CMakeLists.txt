# Unit tests for ghm — pure-function parsers and validators, plus
# integration tests that exercise GitHandler against real temp repos.
#
# Each test is its own small executable. The "unit" tests (parsers)
# share Qt6::Test only — no QApplication, no libgit2 — and run in
# milliseconds. The "integration" tests link the same set of
# GitHandler sources the main app uses, plus libgit2, and create
# real on-disk repos so they're slower (~200ms total) but catch
# regressions the parser tests can't.
#
# Tests are added to CTest via add_test(), so `ctest --output-on-failure`
# in the build directory runs everything.

set(TEST_SRC_DIR ${CMAKE_SOURCE_DIR}/src)

# Helper to register one QtTest-based pure unit test (no libgit2).
function(ghm_add_unit_test test_name)
    add_executable(${test_name} ${test_name}.cpp)
    target_include_directories(${test_name} PRIVATE ${TEST_SRC_DIR})
    target_link_libraries(${test_name} PRIVATE Qt6::Core Qt6::Test)
    target_compile_features(${test_name} PRIVATE cxx_std_20)
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

# Helper for integration tests that exercise GitHandler. These need
# the actual GitHandler sources plus libgit2 to link. We compile the
# GitHandler sources into each test executable rather than into a
# shared library — keeps the build simple, and they're small enough
# (~2K lines) that the duplication doesn't matter.
function(ghm_add_git_integration_test test_name)
    add_executable(${test_name}
        ${test_name}.cpp
        ${TEST_SRC_DIR}/git/GitHandler.cpp
    )
    set_target_properties(${test_name} PROPERTIES
        AUTOMOC ON  # for Q_OBJECT test class
    )
    target_include_directories(${test_name} PRIVATE ${TEST_SRC_DIR})
    target_link_libraries(${test_name} PRIVATE
        Qt6::Core Qt6::Test
        PkgConfig::LIBGIT2
    )
    target_compile_features(${test_name} PRIVATE cxx_std_20)
    target_compile_definitions(${test_name} PRIVATE QT_NO_KEYWORDS)
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

ghm_add_unit_test(test_AddRemoteParser)
ghm_add_unit_test(test_BranchNameValidator)
ghm_add_unit_test(test_RepoNameSuggester)
ghm_add_unit_test(test_LinkHeaderParser)
ghm_add_unit_test(test_SshUrlConverter)
ghm_add_unit_test(test_SshKeyInfo)
ghm_add_unit_test(test_OAuthResponseParser)

ghm_add_git_integration_test(test_GitHandler)
