cmake_minimum_required(VERSION 3.20)
project(sco_counter_service C)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/sco-plugin.cmake")

sco_add_plugin(counter_service SOURCES counter_service.c INI plugin.ini)
