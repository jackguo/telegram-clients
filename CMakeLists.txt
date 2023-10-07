cmake_minimum_required(VERSION 3.4 FATAL_ERROR)

project(TdDownloader VERSION 1.0 LANGUAGES CXX)

find_package(Td 1.8.0 REQUIRED)

add_executable(tdjson_example ./src/tdjson_example.cpp)
target_link_libraries(tdjson_example PRIVATE Td::TdJson)
set_property(TARGET tdjson_example PROPERTY CXX_STANDARD 11)

include_directories(inc)
add_executable(td_downloader ./src/td_downloader.cpp)
add_library(TaskApi ./src/Downloader.cpp
                    ./src/TdTask.cpp
                    ./src/ClientWrapper.cpp
                    ./src/TdMain.cpp)
target_link_libraries(TaskApi PRIVATE Td::TdStatic)
target_link_libraries(td_downloader PRIVATE Td::TdStatic TaskApi)
set_property(TARGET td_downloader PROPERTY CXX_STANDARD 14)
set_property(TARGET TaskApi PROPERTY CXX_STANDARD 14)

add_executable(ngram main.cpp
                     ./src/TdExample.cpp)
target_link_libraries(ngram PRIVATE Td::TdStatic)
set_property(TARGET ngram PROPERTY CXX_STANDARD 17)