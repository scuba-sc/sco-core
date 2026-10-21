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
# The modular pair (sdk/examples/counter_service, counter_panel): counter_panel's folder sorts first,
# but its requires = counter_service.counter loads the provider before it (checked from the host's
# load lines); the invoke then calls the provider's table across plugins.
execute_process(COMMAND "${SIM}" "${ROOT}" --ticks 1 --invoke counter_panel.bump
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
message("${out}${err}")
if(NOT rc STREQUAL "0" OR NOT out MATCHES "counter_panel\\.bump -> OK \"counter is 1\"")
  message("host_sim_examples: FAIL (counter_panel: exit ${rc} or no reply from counter_service's table)")
  message(FATAL_ERROR "counter_panel.bump did not reach counter_service.counter through the real host")
endif()
string(FIND "${out}" "[plugin] loaded counter_service " _provider_at)
string(FIND "${out}" "[plugin] loaded counter_panel " _consumer_at)
if(_provider_at EQUAL -1 OR _consumer_at EQUAL -1 OR NOT _provider_at LESS _consumer_at)
  message("host_sim_examples: FAIL (counter_panel loaded before its provider)")
  message(FATAL_ERROR "requires = counter_service.counter did not order the provider first")
endif()
