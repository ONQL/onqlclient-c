cmake_minimum_required(VERSION 3.10)
project(onql_c_driver VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_library(onql STATIC src/onql.c)
target_include_directories(onql PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if(WIN32)
    target_link_libraries(onql PRIVATE ws2_32)
endif()

# Install targets
install(TARGETS onql ARCHIVE DESTINATION lib)
install(FILES include/onql.h DESTINATION include)

# Optional: build example
option(ONQL_BUILD_EXAMPLE "Build example program" OFF)
if(ONQL_BUILD_EXAMPLE)
    add_executable(onql_example example/main.c)
    target_link_libraries(onql_example PRIVATE onql)
endif()
