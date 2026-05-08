cmake_minimum_required(VERSION 3.31)

# Prefer Homebrew LLVM clang for OpenMP support and fall back to Apple clang.
set(HOMEBREW_CLANG "/opt/homebrew/opt/llvm/bin/clang")
set(HOMEBREW_CLANGXX "/opt/homebrew/opt/llvm/bin/clang++")
set(HOMEBREW_LLVM_PREFIX "/opt/homebrew/opt/llvm")
set(HOMEBREW_LIBOMP_PREFIX "/opt/homebrew/opt/libomp")

if (EXISTS "${HOMEBREW_CLANG}" AND EXISTS "${HOMEBREW_CLANGXX}")
    set(CMAKE_C_COMPILER "${HOMEBREW_CLANG}")
    set(CMAKE_CXX_COMPILER "${HOMEBREW_CLANGXX}")
endif ()

project(LLVM_MinML_Parser VERSION 1.0.0)
enable_testing()

if (APPLE)
    if (EXISTS "${HOMEBREW_LLVM_PREFIX}")
        list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_LLVM_PREFIX}")
    endif ()
    if (EXISTS "${HOMEBREW_LIBOMP_PREFIX}")
        list(APPEND CMAKE_PREFIX_PATH "${HOMEBREW_LIBOMP_PREFIX}")
    endif ()
endif ()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE "Release")

file(GLOB CLASSIFIER_SOURCES CONFIGURE_DEPENDS
    src/classifiers/*.cpp
)

# Define the header-forge executable
add_executable(${PROJECT_NAME}
    main.cpp
    src/Parser.cpp
    src/EmbeddedStats.cpp
    src/NestedStats.cpp
    include/Parser.hpp
    src/Stats.cpp
    include/EmbeddedStats.hpp
    include/NestedStats.hpp
    include/Stats.hpp
    include/MatcherText.hpp
    include/AtomicString.hpp
    src/FileStats.cpp
    include/FileStats.hpp
    include/StatsGenerator.hpp
    src/LanguageClassifier.cpp
    ${CLASSIFIER_SOURCES}
    src/LanguageStats.cpp
    include/LanguageClassifier.hpp
    include/LanguageStats.hpp
    include/LanguageModel.generated.hpp
    src/LanguageParser.cpp
    include/LanguageParser.hpp
)

add_executable(LanguageClassifierTests
    tests/language_detection_test.cpp
    src/LanguageClassifier.cpp
    ${CLASSIFIER_SOURCES}
    include/LanguageClassifier.hpp
    include/LanguageModel.generated.hpp
)
target_include_directories(LanguageClassifierTests PRIVATE include)
add_test(NAME language_detection COMMAND LanguageClassifierTests)

add_executable(ParserFixtureTests
    tests/parser_fixture_test.cpp
    src/Parser.cpp
    src/EmbeddedStats.cpp
    src/NestedStats.cpp
    src/Stats.cpp
    src/FileStats.cpp
    src/LanguageClassifier.cpp
    ${CLASSIFIER_SOURCES}
    src/LanguageStats.cpp
    include/Parser.hpp
    include/EmbeddedStats.hpp
    include/NestedStats.hpp
    include/Stats.hpp
    include/MatcherText.hpp
    include/AtomicString.hpp
    include/FileStats.hpp
    include/StatsGenerator.hpp
    include/LanguageClassifier.hpp
    include/LanguageStats.hpp
    include/LanguageModel.generated.hpp
    src/LanguageParser.cpp
    include/LanguageParser.hpp
)
target_include_directories(ParserFixtureTests PRIVATE include)
target_link_libraries(ParserFixtureTests clangLex clangBasic)
if (OpenMP_CXX_FOUND)
    target_compile_definitions(ParserFixtureTests PRIVATE USE_OPENMP=1)
    target_link_libraries(ParserFixtureTests OpenMP::OpenMP_CXX)
else ()
    target_compile_definitions(ParserFixtureTests PRIVATE USE_OPENMP=0)
endif ()
add_test(NAME parser_fixture_basic_literals
    COMMAND ParserFixtureTests ${CMAKE_SOURCE_DIR}/tests/basic_literals.cpp)
add_test(NAME parser_fixture_raw_and_adjacent
    COMMAND ParserFixtureTests ${CMAKE_SOURCE_DIR}/tests/raw_and_adjacent.cpp)
add_test(NAME parser_fixture_comments_and_relaxed
    COMMAND ParserFixtureTests ${CMAKE_SOURCE_DIR}/tests/comments_and_relaxed.cpp)
add_test(NAME parser_fixture_prefixed_literals
    COMMAND ParserFixtureTests ${CMAKE_SOURCE_DIR}/tests/prefixed_literals.cpp)
add_test(NAME parser_fixture_string_bucket_mix
    COMMAND ParserFixtureTests ${CMAKE_SOURCE_DIR}/tests/string_bucket_mix.cpp)

add_executable(DebugLanguageOutputTests
    tests/debug_language_output_test.cpp
    src/Parser.cpp
    src/EmbeddedStats.cpp
    src/NestedStats.cpp
    src/Stats.cpp
    src/FileStats.cpp
    src/LanguageClassifier.cpp
    ${CLASSIFIER_SOURCES}
    src/LanguageStats.cpp
    include/Parser.hpp
    include/EmbeddedStats.hpp
    include/NestedStats.hpp
    include/Stats.hpp
    include/MatcherText.hpp
    include/AtomicString.hpp
    include/FileStats.hpp
    include/StatsGenerator.hpp
    include/LanguageClassifier.hpp
    include/LanguageStats.hpp
    include/LanguageModel.generated.hpp
    src/LanguageParser.cpp
    include/LanguageParser.hpp
)
target_include_directories(DebugLanguageOutputTests PRIVATE include)
target_link_libraries(DebugLanguageOutputTests clangLex clangBasic)
if (OpenMP_CXX_FOUND)
    target_compile_definitions(DebugLanguageOutputTests PRIVATE USE_OPENMP=1)
    target_link_libraries(DebugLanguageOutputTests OpenMP::OpenMP_CXX)
else ()
    target_compile_definitions(DebugLanguageOutputTests PRIVATE USE_OPENMP=0)
endif ()
add_test(NAME debug_language_output
    COMMAND DebugLanguageOutputTests
    ${CMAKE_SOURCE_DIR}/tests/string_bucket_mix.cpp
    ${CMAKE_BINARY_DIR}/debug-language-output)

# === LLVM & Clang ===
find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

include_directories(${LLVM_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} clangLex clangBasic)
target_compile_definitions(${PROJECT_NAME} PRIVATE MATCHERTEXT_PARSERS_DIR="${CMAKE_SOURCE_DIR}/parsers")

# === Go parser binary ===
# Pre-compile parsers/parser.go into a native binary so each per-file invocation
# avoids recompiling, and dodges `go run`'s rule that disallows .go arguments
# from a different directory than the source file.
find_program(GO_EXECUTABLE go)
if (GO_EXECUTABLE)
    set(GO_PARSER_BIN "${CMAKE_BINARY_DIR}/matchertext_go_parser")
    add_custom_command(
        OUTPUT ${GO_PARSER_BIN}
        COMMAND ${GO_EXECUTABLE} build -o ${GO_PARSER_BIN} ${CMAKE_SOURCE_DIR}/parsers/parser.go
        DEPENDS ${CMAKE_SOURCE_DIR}/parsers/parser.go
        COMMENT "Building Go parser binary"
        VERBATIM
    )
    add_custom_target(go_parser ALL DEPENDS ${GO_PARSER_BIN})
    add_dependencies(${PROJECT_NAME} go_parser)
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        MATCHERTEXT_GO_PARSER_BIN="${GO_PARSER_BIN}")
else ()
    message(WARNING "go executable not found; Go parsing will be disabled at runtime")
endif ()

# === OpenMP ===
find_package(OpenMP QUIET)
if (OpenMP_CXX_FOUND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_OPENMP=1)
    target_link_libraries(${PROJECT_NAME} OpenMP::OpenMP_CXX)
else ()
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_OPENMP=0)
    message(STATUS "OpenMP not found, building without parallel parsing support")
endif ()

# === Serde ===
add_subdirectory(extern/Serde)
target_link_libraries(${PROJECT_NAME} Serde)
target_link_libraries(LanguageClassifierTests Serde)
target_link_libraries(ParserFixtureTests Serde)
target_link_libraries(DebugLanguageOutputTests Serde)

# --------------------------
# Build Configs
# --------------------------

if (CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -O3 -DNDEBUG -finline-functions -fomit-frame-pointer
        -ffunction-sections -fdata-sections -flto -Wno-c99-designator
    )
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,-O2)
    set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD COMMAND ${CMAKE_STRIP} $<TARGET_FILE:${PROJECT_NAME}>)
elseif (CMAKE_BUILD_TYPE STREQUAL "Debug")
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        set(SANITIZER_FLAGS "-fsanitize=address -fno-omit-frame-pointer")
        set(EXTRA_DEBUG_FLAGS "-g -O1")
        set(ASAN_OPTIONS_FLAGS "-fsanitize-recover=address -fsanitize=undefined -fsanitize-address-use-after-scope")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${SANITIZER_FLAGS} ${EXTRA_DEBUG_FLAGS} ${ASAN_OPTIONS_FLAGS}")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${SANITIZER_FLAGS} ${EXTRA_DEBUG_FLAGS} ${ASAN_OPTIONS_FLAGS}")
        set(CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} ${SANITIZER_FLAGS} ${EXTRA_DEBUG_FLAGS} ${ASAN_OPTIONS_FLAGS}")
        add_compile_definitions(ASAN_OPTIONS="verbosity=1:halt_on_error=1:detect_leaks=1:symbolize=1")
    endif ()
endif ()

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${PROJECT_NAME}> ${CMAKE_SOURCE_DIR}/parser
)