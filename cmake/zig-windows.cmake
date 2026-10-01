set(CMAKE_SYSTEM_NAME Windows)

find_program(MODLOCK_ZIG_EXECUTABLE zig REQUIRED)
# Compiler checks reload this file in their own projects. Forward the chosen
# Zig so they use it even when zig is not on PATH.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES MODLOCK_ZIG_EXECUTABLE)
# Invoke Zig directly so archive rules work in both native Windows and Unix
# cross-builds, including source distributed without executable mode bits.
set(CMAKE_AR "${MODLOCK_ZIG_EXECUTABLE}" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${MODLOCK_ZIG_EXECUTABLE}" CACHE FILEPATH "" FORCE)
# Windows-Clang initializes archive rules after the toolchain is loaded.
set(CMAKE_USER_MAKE_RULES_OVERRIDE "${CMAKE_CURRENT_LIST_DIR}/zig-windows-rules.cmake")

set(CMAKE_C_COMPILER "${MODLOCK_ZIG_EXECUTABLE}" CACHE FILEPATH "")
set(CMAKE_C_COMPILER_ARG1 cc CACHE STRING "")
set(CMAKE_CXX_COMPILER "${MODLOCK_ZIG_EXECUTABLE}" CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER_ARG1 c++ CACHE STRING "")

if(DEFINED ENV{MODLOCK_ZIG_TARGET} AND NOT "$ENV{MODLOCK_ZIG_TARGET}" STREQUAL "")
  set(MODLOCK_ZIG_TARGET "$ENV{MODLOCK_ZIG_TARGET}")
else()
  set(MODLOCK_ZIG_TARGET x86_64-windows-gnu)
endif()

set(CMAKE_C_FLAGS_INIT "-target ${MODLOCK_ZIG_TARGET}")
# Zig's MinGW libc++ enables the removed std::unexpected() function. Disable it
# so C++23 std::unexpected error values are unambiguous.
set(CMAKE_CXX_FLAGS_INIT
    "-target ${MODLOCK_ZIG_TARGET} -U_LIBCPP_ENABLE_CXX17_REMOVED_UNEXPECTED_FUNCTIONS")

# Abseil's unconditional find_library(rt) can discover the Linux host library
# while cross-compiling. Windows has no librt and must never link the host copy.
set(LIBRT "" CACHE STRING "" FORCE)
