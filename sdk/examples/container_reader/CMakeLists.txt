cmake_minimum_required(VERSION 3.20)
project(sco_container_reader C)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/sco-plugin.cmake")
sco_add_plugin(container_reader SOURCES container_reader.c INI plugin.ini)
