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
    src/LanguageStats.cpp
    include/LanguageClassifier.hpp
    include/LanguageStats.hpp
    include/LanguageModel.generated.hpp
)

# === LLVM & Clang ===
find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

include_directories(${LLVM_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} clangLex clangBasic)

# === OpenMP ===
find_package(OpenMP QUIET)
if (OpenMP_CXX_FOUND)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_OPENMP=1)
    target_link_libraries(${PROJECT_NAME} OpenMP::OpenMP_CXX)
else ()
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_OPENMP=0)
    message(STATUS "OpenMP not found, building without parallel parsing support")
endif ()

# --------------------------
# Build Configs
# --------------------------

if (CMAKE_BUILD_TYPE STREQUAL "Release")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -O3 -DNDEBUG -finline-functions -fomit-frame-pointer
        -ffunction-sections -fdata-sections -flto
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
