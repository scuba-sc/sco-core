# SPDX-License-Identifier: GPL-3.0-only
# CTest host_sim_examples: C and C++ SDK examples through the real neutral host.
foreach(plugin IN ITEMS hello cpp_hello)
  execute_process(COMMAND "${SIM}" "${ROOT}" --ticks 3 --invoke "${plugin}.wave" "Pilot One"
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  message("${out}${err}")
  if(NOT rc STREQUAL "0" OR NOT out MATCHES "${plugin}\\.wave -> OK \"Hello, Pilot One\"")
    message("host_sim_examples: FAIL (${plugin}: exit ${rc} or missing command reply)")
    message(FATAL_ERROR "${plugin}.wave did not answer through the real host")
  endif()
  if(plugin STREQUAL "cpp_hello" AND NOT out MATCHES "Hello, services")
    message("host_sim_examples: FAIL (C++ service lookup)")
    message(FATAL_ERROR "cpp_hello's published service did not resolve")
  endif()
endforeach()
