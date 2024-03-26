cmake_minimum_required(VERSION 3.4 FATAL_ERROR)

project(TdDownloader VERSION 1.0 LANGUAGES CXX)

find_package(Td 1.8.26 REQUIRED)

include_directories(inc)
add_executable(td_downloader td_downloader.cpp)
add_library(TaskApi 
			TdTask.cpp
			ClientWrapper.cpp
			Downloader.cpp
			TdMain.cpp
			inc/task_api.h)
target_link_libraries(TaskApi PRIVATE Td::TdStatic)
target_link_libraries(td_downloader PRIVATE Td::TdStatic TaskApi)
set_property(TARGET td_downloader PROPERTY CXX_STANDARD 14)
set_property(TARGET TaskApi PROPERTY CXX_STANDARD 14)
