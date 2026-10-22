cmake_minimum_required(VERSION 3.20)
project(sco_counter_panel C)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/sco-plugin.cmake")

# counter_panel.c includes ../counter_service/counter_service.h, the provider's contract header.
sco_add_plugin(counter_panel SOURCES counter_panel.c INI plugin.ini)
