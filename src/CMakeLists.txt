include_directories(SYSTEM "${Boost_INCLUDE_DIRS}")

#Make sure gen-cpp files can be included
include_directories("${CMAKE_CURRENT_BINARY_DIR}")
include_directories("${CMAKE_CURRENT_BINARY_DIR}/gen-cpp")
include_directories("${PROJECT_SOURCE_DIR}/lib/cpp/src")

set(gencpp_SOURCES
    gen-cpp/Calculator.cpp
    gen-cpp/SharedService.cpp
    gen-cpp/shared_constants.cpp
    gen-cpp/shared_types.cpp
    gen-cpp/tutorial_constants.cpp
    gen-cpp/tutorial_types.cpp
)

add_custom_command(OUTPUT ${gencpp_SOURCES}
    COMMAND thrift --gen cpp -r ${PROJECT_SOURCE_DIR}/src/tutorial.thrift
)

add_library(tutorialgencpp STATIC ${gencpp_SOURCES})
target_link_libraries(tutorialgencpp thrift)

add_executable(Server CppServer.cpp)
target_link_libraries(Server tutorialgencpp thrift thriftnb event ${Boost_LIBRARIES})
if (ZLIB_FOUND)
  target_link_libraries(Server ${ZLIB_LIBRARIES})
endif ()

add_executable(Client CppClient.cpp)
target_link_libraries(Client tutorialgencpp thrift ${Boost_LIBRARIES})
if (ZLIB_FOUND)
  target_link_libraries(Client ${ZLIB_LIBRARIES})
endif ()
