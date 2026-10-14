# SPDX-License-Identifier: GPL-3.0-only
# CTest plugin_check_services: sco-plugin-check's stand-in host on the checker_probe plugins
# (tests/fixtures/plugins/native/checker_probe.c). expect(<id> <exit code> <output regex> [option]...).
function(expect id rc pattern)
  execute_process(COMMAND "${CHECK}" "${ROOT}/${id}" ${ARGN}
                  RESULT_VARIABLE got OUTPUT_VARIABLE out ERROR_VARIABLE err)
  message("${out}${err}")
  if(NOT got STREQUAL "${rc}" OR NOT out MATCHES "${pattern}")
    message("plugin_check_services: FAIL (${id} ${ARGN}: exit ${got}, wanted ${rc} and /${pattern}/)")
    message(FATAL_ERROR "sco-plugin-check misjudged ${id}")
  endif()
endfunction()

# release_service withdraws the plugin's own service; on a queried (borrowed) name it is a misuse.
expect(chk_release_own 0 "  release chk_release_own\\.svc")
expect(chk_release_borrowed 1 "FAIL release_service 'sco\\.widgets': the plugin queried this service but does not provide it")
# --short-tables: a 1.1 member called without a size check fails; the same plugin passes a full table,
# and a plugin that checks size >= offsetof + sizeof passes both.
expect(chk_tail_unchecked 0 "widgets found")
expect(chk_tail_unchecked 1 "FAIL plugin called sco_api\\.query_service, but this host's sco_api\\.size is 88" --short-tables)
expect(chk_tail_checked 0 "1\\.0 host: no services" --short-tables)
expect(chk_tail_checked 0 "widgets found")
# sco.widgets 1.1: served by default, validated as the host does (the probe fails its load on a wrong
# answer); --short-tables serves the 1.0 extent (size 24), where an unchecked 1.1 call fails.
expect(chk_w11_unchecked 0 "widgets 1\\.1 ok features=0x7f")
expect(chk_w11_unchecked 1 "FAIL plugin called sco_widgets_v1\\.update_nodes, but this host's sco\\.widgets table size is 24" --short-tables)
expect(chk_w11_checked 0 "widgets 1\\.1 ok features=0x7f.*event kind=6 size=40 row=1.*event kind=2 size=40 row=-1")
expect(chk_w11_checked 0 "1\\.0 host: no services" --short-tables)
