-- SPDX-License-Identifier: GPL-3.0-only
-- Synthetic, renderer-neutral example. No game services, files, FFI or renderer.
local count = 0
assert(sco.register_command{name="widgets_lua.ping", title="Ping",
  fn=function() return "pong" end})
assert(sco.register_command{name="widgets_lua.state", title="Widget events",
  fn=function() return count .. " events" end})

if sco.widgets.available() then
  assert(sco.widgets.set_panel("widgets_lua.main", "Lua widgets", {
    {kind="column", id=1},
    {kind="row", id=2, parent=1},
    {kind="label", id=3, parent=2, label="Synthetic controls"},
    {kind="button", id=4, parent=2, label="Ping"},
    {kind="toggle", id=5, parent=1, label="Enabled", value=false},
    {kind="slider", id=6, parent=1, label="Speed", value=0.5, minimum=0, maximum=1},
    {kind="combo", id=7, parent=1, label="Mode", items={"Alpha","Beta"}, index=0},
    {kind="list", id=8, parent=1, label="Choices", items={"One","Two"}, index=-1},
    {kind="text_input", id=9, parent=1, label="Name", text="Initial"},
    {kind="table", id=10, parent=1, columns=2, items={"Name","Value","Example","42"}}
  }, function(panel, event)
    count = count + 1
    if event.kind == "button" then assert(sco.invoke("widgets_lua.ping")) end
    sco.log("info", panel .. " " .. event.kind .. " " .. event.node_id)
  end))
else
  sco.log("info", "widgets unavailable; commands remain usable")
end
-- Ordinary owner unload removes this panel before closing the Lua state.
