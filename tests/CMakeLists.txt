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

# Python bindings smoke test (plain script, no pytest dependency). Runs when
# the bindings were built; PYTHONPATH points at the module output directory.
if(XTM_BUILD_PYTHON)
    add_test(
        NAME python_bindings
        COMMAND ${Python_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/python/test_xtm_bindings.py
    )
    set_tests_properties(python_bindings PROPERTIES
        ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}/lib"
    )
endif()
