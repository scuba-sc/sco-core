# Container contents reader

Public-header-only C plugin. Build with the SDK's sco-plugin.cmake helper, load
in a compatible host, then invoke `container_reader.read <session-container-id>`.
It re-queries the optional service each time; unavailable native backends refuse
instead of returning empty data. A matching scuba-engine candidate provides
synthetic containers 289 (two items) and 290 (empty). These IDs are fixture-only.
No private headers, game addresses, hooks or inferred native layouts are used.
