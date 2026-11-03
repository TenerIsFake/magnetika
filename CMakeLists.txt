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

option(MAGNETIKA_SANITIZE "Build everything with AddressSanitizer + UBSan (GCC/Clang)" OFF)
if(MAGNETIKA_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=address,undefined)
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

# render-cli on hand-written traces: a short last block must not overrun (run
# under -DMAGNETIKA_SANITIZE=ON to see an overrun), and bad input must be refused.
set(FIX ${CMAKE_SOURCE_DIR}/tests/fixtures)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/fixture-out)
add_test(NAME render-cli-short-last-block
  COMMAND render-cli ${FIX}/odd-frames.txt ${CMAKE_BINARY_DIR}/fixture-out)
add_test(NAME render-cli-rejects-bad-position
  COMMAND render-cli ${FIX}/bad-position.txt ${CMAKE_BINARY_DIR}/fixture-out)
add_test(NAME render-cli-rejects-bad-note
  COMMAND render-cli ${FIX}/bad-note.txt ${CMAKE_BINARY_DIR}/fixture-out)
add_test(NAME render-cli-rejects-missing-out-dir
  COMMAND render-cli ${FIX}/odd-frames.txt ${CMAKE_BINARY_DIR}/no-such-dir)
add_test(NAME render-cli-rejects-unknown-option
  COMMAND render-cli ${FIX}/odd-frames.txt ${CMAKE_BINARY_DIR}/fixture-out --armd)
set_tests_properties(render-cli-rejects-bad-position render-cli-rejects-bad-note
  render-cli-rejects-missing-out-dir render-cli-rejects-unknown-option PROPERTIES WILL_FAIL TRUE)

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

  # compare.mjs must never pass vacuously. Each of these has to FAIL.
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/empty)
  # A copy at a path containing a space: an earlier "am I main?" guard made the
  # script exit 0 without comparing anything from such a path.
  file(COPY ${CMAKE_SOURCE_DIR}/tools/compare.mjs DESTINATION "${CMAKE_BINARY_DIR}/dir with space")
  add_test(NAME compare-fails-on-missing-port-renders
    COMMAND ${NODE} "${CMAKE_BINARY_DIR}/dir with space/compare.mjs" renders ${CMAKE_BINARY_DIR}/empty --max-error-db=-150
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME compare-rejects-empty-limit
    COMMAND ${NODE} tools/compare.mjs renders ${CMAKE_BINARY_DIR}/out --max-error-db=
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME compare-rejects-limit-without-equals
    COMMAND ${NODE} tools/compare.mjs renders ${CMAKE_BINARY_DIR}/out --max-error-db -150
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  set_tests_properties(compare-fails-on-missing-port-renders compare-rejects-empty-limit
    compare-rejects-limit-without-equals PROPERTIES WILL_FAIL TRUE FIXTURES_REQUIRED port)
else()
  message(WARNING "node not found: the port-vs-reference tests are disabled")
endif()
