cmake_minimum_required(VERSION 3.20)
project(sco_actor_stats_probe C)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/sco-plugin.cmake")

sco_add_plugin(actor_stats_probe SOURCES actor_stats_probe.c INI plugin.ini)
