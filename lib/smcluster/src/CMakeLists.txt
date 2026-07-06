add_library(smcluster 
  smcluster.cpp
  conn.cpp

  remote_process/remote_process.cpp
  remote_process/vars_sharing.cpp
  remote_process/ops.cpp
  remote_process/recovery.cpp
  remote_process/utils.cpp

  shared_vars/shared_variable.cpp
  shared_vars/local_shared_variable.cpp
  shared_vars/remote_shared_variable.cpp

  utils/logging.cpp
  utils/mem_v.cpp
  utils/smcluster_parse_config.cpp
)

target_include_directories(smcluster PUBLIC
  "${PROJECT_SOURCE_DIR}/lib/smcluster/include"
  "${PROJECT_SOURCE_DIR}/lib/smcluster/include/utils"
  "${PROJECT_SOURCE_DIR}/lib/smcluster/src"
)

target_link_libraries(smcluster PRIVATE rdmacm ibverbs)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_definitions(smcluster PUBLIC DEBUG)
endif()

if(${PROFILING} STREQUAL "True")
  target_compile_definitions(smcluster PUBLIC PROFILING)
endif()
