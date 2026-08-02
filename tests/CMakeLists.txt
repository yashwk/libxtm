file(GLOB_RECURSE XTM_TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp
)

add_executable(xtm_tests ${XTM_TEST_SOURCES})

target_link_libraries(xtm_tests PRIVATE
    xtm_core
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(xtm_tests)
