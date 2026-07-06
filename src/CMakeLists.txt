add_library(tree "tree/tree.cpp")

target_include_directories(tree PUBLIC
    "${PROJECT_SOURCE_DIR}/lib/smcluster/include"
    "${PROJECT_SOURCE_DIR}/lib/smcluster/include/utils"
)

add_library(tram
    tram_process.cpp

    client/tram_client.cpp

    server/tram_server.cpp
    server/leader.cpp
    server/follower.cpp
    server/failover.cpp
    server/remote_child.cpp
    server/remote_client.cpp
    server/remote_parent.cpp
    server/remote_replica.cpp

    utils/datatypes.cpp
    utils/tram_parse_config.cpp
)

target_include_directories(tram PUBLIC 
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/include/client"
    "${PROJECT_SOURCE_DIR}/include/server"
    "${PROJECT_SOURCE_DIR}/include/structures"
    "${PROJECT_SOURCE_DIR}/include/utils"

    "${PROJECT_SOURCE_DIR}/src/tree"

    "${PROJECT_SOURCE_DIR}/lib/smcluster/include"
    "${PROJECT_SOURCE_DIR}/lib/smcluster/include/utils"
)

target_link_libraries(tram PRIVATE smcluster rdmacm ibverbs tree)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(tram PUBLIC DEBUG)
  target_compile_definitions(tram PUBLIC ASAN_OPTIONS="detect_leaks=1:detect_stack_use_after_return=1:fast_unwind_on_malloc=0")
endif()

if(${PROFILING} STREQUAL "True")
  target_compile_definitions(tram PUBLIC PROFILING)
endif()
