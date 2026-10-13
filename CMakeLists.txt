cmake_minimum_required(VERSION 3.25)

project(kestrel VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(KESTREL_BUILD_TESTS "Build unit tests" ON)
option(KESTREL_WERROR "Treat warnings as errors" OFF)
option(KESTREL_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

add_library(kestrel_options INTERFACE)
target_compile_options(kestrel_options INTERFACE
  -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
  -Wnon-virtual-dtor -Wold-style-cast -Wimplicit-fallthrough)
if(KESTREL_WERROR)
  target_compile_options(kestrel_options INTERFACE -Werror)
endif()
if(KESTREL_SANITIZE)
  target_compile_options(kestrel_options INTERFACE
    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
  target_link_options(kestrel_options INTERFACE -fsanitize=address,undefined)
endif()

add_library(kestrel_core
  src/frontend/lexer.cpp
  src/support/arena.cpp
  src/support/diagnostics.cpp
)
target_include_directories(kestrel_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(kestrel_core PUBLIC kestrel_options)

if(KESTREL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

add_executable(kestrel tools/kestrel/main.cpp)
target_link_libraries(kestrel PRIVATE kestrel_core)
