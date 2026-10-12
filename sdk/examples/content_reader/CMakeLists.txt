cmake_minimum_required(VERSION 3.20)
project(sco_content_reader C)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/sco-plugin.cmake")
sco_add_plugin(content_reader SOURCES content_reader.c INI plugin.ini)
