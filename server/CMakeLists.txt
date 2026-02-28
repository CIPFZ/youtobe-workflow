cmake_minimum_required(VERSION 3.16)
project(av_workflow_service LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

option(ENABLE_WORKFLOW "Enable sogou/workflow integration" ON)
option(ENABLE_FFMPEG "Enable FFmpeg libavformat/libavcodec integration" ON)

set(WORKFLOW_READY OFF)
if (ENABLE_WORKFLOW)
  find_path(WORKFLOW_INCLUDE_DIR workflow/WFHttpServer.h)
  find_library(WORKFLOW_LIBRARY NAMES workflow)
  if (WORKFLOW_INCLUDE_DIR AND WORKFLOW_LIBRARY)
    message(STATUS "Found workflow include dir: ${WORKFLOW_INCLUDE_DIR}")
    message(STATUS "Found workflow library: ${WORKFLOW_LIBRARY}")
    set(WORKFLOW_READY ON)
    add_compile_definitions(HAVE_WORKFLOW=1)
  else()
    message(WARNING "workflow headers/library not found; building fallback mode")
  endif()
endif()

set(FFMPEG_READY OFF)
if (ENABLE_FFMPEG)
  pkg_check_modules(FFMPEG IMPORTED_TARGET libavformat libavcodec libavutil)
  if (FFMPEG_FOUND)
    message(STATUS "Found FFmpeg via pkg-config")
    set(FFMPEG_READY ON)
    add_compile_definitions(HAVE_FFMPEG=1)
  else()
    message(WARNING "FFmpeg development libraries not found; building simulated worker")
  endif()
endif()

add_library(av_core
  src/task_manager.cpp
  src/merge_worker.cpp
  src/api_server.cpp
)

target_include_directories(av_core PUBLIC include)

target_link_libraries(av_core PUBLIC Threads::Threads)

if (WORKFLOW_READY)
  target_include_directories(av_core PUBLIC ${WORKFLOW_INCLUDE_DIR})
  target_link_libraries(av_core PUBLIC ${WORKFLOW_LIBRARY})
endif()

if (FFMPEG_READY)
  target_link_libraries(av_core PUBLIC PkgConfig::FFMPEG)
endif()

add_executable(av_service src/main.cpp)
target_link_libraries(av_service PRIVATE av_core)

enable_testing()

add_executable(test_task_manager tests/test_task_manager.cpp)
target_link_libraries(test_task_manager PRIVATE av_core)
add_test(NAME test_task_manager COMMAND test_task_manager)

add_executable(test_merge_worker tests/test_merge_worker.cpp)
target_link_libraries(test_merge_worker PRIVATE av_core)
add_test(NAME test_merge_worker COMMAND test_merge_worker)

add_executable(test_integration_flow tests/test_integration_flow.cpp)
target_link_libraries(test_integration_flow PRIVATE av_core)
add_test(NAME test_integration_flow COMMAND test_integration_flow)
