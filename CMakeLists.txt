cmake_minimum_required(VERSION 3.22)
project(magnetika VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The engine is checked against the JS reference to the last bit where possible,
# so the compiler must not reassociate or fuse floating-point operations.
# Never add -ffast-math or /fp:fast to anything that links the engine.
if(MSVC)
  set(MAGNETIKA_FP_FLAGS /fp:precise /fp:contract-)
  set(MAGNETIKA_WARN /W4)
else()
  set(MAGNETIKA_FP_FLAGS -ffp-contract=off -fno-fast-math)
  set(MAGNETIKA_WARN -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

add_library(magnetika_engine STATIC
  engine/src/params.cpp
  engine/src/bowl.cpp
  engine/src/bar.cpp
  engine/src/synth.cpp
)
target_include_directories(magnetika_engine PUBLIC engine/include)
target_compile_options(magnetika_engine PRIVATE ${MAGNETIKA_FP_FLAGS} ${MAGNETIKA_WARN})

add_executable(render-cli tools/render-cli/main.cpp)
target_link_libraries(render-cli PRIVATE magnetika_engine)
target_compile_options(render-cli PRIVATE ${MAGNETIKA_FP_FLAGS} ${MAGNETIKA_WARN})

# ---------------------------------------------------------------- tests
enable_testing()

add_executable(test-engine engine/tests/test_engine.cpp)
target_link_libraries(test-engine PRIVATE magnetika_engine)
target_compile_options(test-engine PRIVATE ${MAGNETIKA_FP_FLAGS} ${MAGNETIKA_WARN})
add_test(NAME engine COMMAND test-engine)

# The port against the JS reference. Needs Node; `npm run reference` must have
# produced renders/ (the fixture step below regenerates it).
find_program(NODE node)
if(NODE)
  add_test(NAME reference-render
    COMMAND ${NODE} reference/render.mjs
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  set_tests_properties(reference-render PROPERTIES FIXTURES_SETUP reference)
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/out)
  add_test(NAME port-render
    COMMAND render-cli ${CMAKE_SOURCE_DIR}/renders/trace.txt ${CMAKE_BINARY_DIR}/out)
  set_tests_properties(port-render PROPERTIES FIXTURES_REQUIRED reference FIXTURES_SETUP port)
  add_test(NAME port-matches-reference
    COMMAND ${NODE} tools/compare.mjs renders ${CMAKE_BINARY_DIR}/out --max-error-db=-150
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  set_tests_properties(port-matches-reference PROPERTIES FIXTURES_REQUIRED port)
else()
  message(WARNING "node not found: the port-vs-reference tests are disabled")
endif()
