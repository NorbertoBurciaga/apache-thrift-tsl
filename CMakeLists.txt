cmake_minimum_required(VERSION 3.10)

project(thriftTSL)

set (VERSION_MAJOR 0)
set (VERSION_MINOR 1)
set (VERSION_PATCH 0)

FIND_PACKAGE(Boost 1.65 REQUIRED COMPONENTS
#    atomic
#    crono
#    container
#    context
#    coroutine
#    date_time
#    exception
    filesystem
#    graph
#    iostreams
#    locale
#    log
#    log_setup
#    math_c99
#    math_c99f
#    math_c99l
#    math_tr1
#    math_tr1f
#    math_tr1l
#    prg_exec_monitor
#    program_options
#    python
#    random
#    regex
#    serialization
#    signals
#    stacktrace_addr2line
#    stacktrace_backtrace
#    stacktrace_basic
#    stacktrace_noop
#    system
#    test_exec_monitor
#    thread
#    timer
#    type_erasure
#    unit_test_framework
#    wave
#    wserialization
)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++0x")

# Output directory settings
set(output_dir ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${output_dir})

include_directories(${CMAKE_CURRENT_BINARY_DIR}/gen-cpp)

add_subdirectory(src)

