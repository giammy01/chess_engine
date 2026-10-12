cmake_minimum_required(VERSION 3.16)
project(nitro CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# -O3 and -march=native matter enormously here: the search is pure integer and
# bitboard work, and native builds use your CPU's popcount/pext instructions.
# Expect roughly a 2x loss without them.
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-march=native" HAS_MARCH_NATIVE)
if(HAS_MARCH_NATIVE)
  add_compile_options(-march=native)
endif()

add_executable(nitro src/main.cpp)
target_include_directories(nitro PRIVATE src)

# Optional: link-time optimisation, another few percent.
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_ok OUTPUT ipo_msg)
if(ipo_ok)
  set_property(TARGET nitro PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

enable_testing()
add_test(NAME perft   COMMAND nitro perft 5)
add_test(NAME tactics COMMAND nitro selftest 1000)
