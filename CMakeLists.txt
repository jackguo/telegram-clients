cmake_minimum_required(VERSION 3.4 FATAL_ERROR)

project(TdDownloader VERSION 1.0 LANGUAGES CXX)

find_package(Td 1.8.0 REQUIRED)

add_executable(tdjson_example tdjson_example.cpp)
target_link_libraries(tdjson_example PRIVATE Td::TdJson)
set_property(TARGET tdjson_example PROPERTY CXX_STANDARD 11)

include_directories($TdDownloader_SOURCE_DIR/inc)
add_executable(td_downloader td_downloader.cpp)
add_library(TaskApi task_api.cpp)
target_link_libraries(TaskApi PRIVATE Td::TdStatic)
target_link_libraries(td_downloader PRIVATE Td::TdStatic TaskApi)
set_property(TARGET td_downloader PROPERTY CXX_STANDARD 14)

add_executable(td_example td_example.cpp)
target_link_libraries(td_example PRIVATE Td::TdStatic)
set_property(TARGET td_example PROPERTY CXX_STANDARD 14)