# SPDX-License-Identifier: GPL-3.0-only
cmake_minimum_required(VERSION 3.20)
project(sco_widgets_lua NONE)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/sco-plugin.cmake")
sco_add_pack(widgets_lua DIR "${CMAKE_CURRENT_LIST_DIR}")
