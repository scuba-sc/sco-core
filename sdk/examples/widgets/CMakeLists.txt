cmake_minimum_required(VERSION 3.20)
project(sco_widgets_example C)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/sco-plugin.cmake")
sco_add_plugin(widgets SOURCES widgets.c INI plugin.ini)
