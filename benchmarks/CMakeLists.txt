if(${BUILD_TESTS} STREQUAL "True")
  add_executable(benchmarks benchmarks.cpp)
  target_link_libraries(benchmarks PUBLIC tram)
endif()
