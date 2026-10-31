-- notebook: the sco SDK's storage example.
--
-- sco.store is the plugin's own database (the host service sco.storage): key-value and SQL,
-- kept across launches, invisible to other plugins. Calls return their value, or nil, the
-- error name ("bad_arg", "unavailable", ...) and the host's message. On a host without
-- storage every call answers "unavailable".

local store = sco.store

-- Raises with the error name and message: a command that raises fails with that reply.
local function check(value, err, msg)
  if err ~= nil then error("storage: " .. err .. (msg and (": " .. msg) or ""), 2) end
  return value
end

-- Roll back ordinary operation/commit failures. Forced interruption can still
-- prevent script cleanup; ordinary unload/non-retained crash cleanup is host-owned.
-- A retained-provider fault can require restart and does not promise cleanup.
local function transaction(fn)
  check(store.begin())
  local ok, result = pcall(function()
    local value = fn()
    check(store.commit())
    return value
  end)
  if not ok then
    local rolled, err, msg = store.rollback()
    if not rolled then
      error(tostring(result) .. "; rollback: " .. tostring(err) .. (msg and (": " .. msg) or ""), 0)
    end
    error(result, 0)
  end
  return result
end

local function notes(n) return n .. (n == 1 and " note" or " notes") end

local function count()
  return tonumber(check(store.get("count")) or "0")
end

-- notebook.add <text>: keeps a note. The note and the counter change together or not at all.
sco.register_command{
  name  = "notebook.add",
  title = "Add a note",
  args  = {{ name = "text", type = "string", help = "The note" }},
  fn = function(text)
    local n = count() + 1
    transaction(function()
      check(store.put(string.format("note.%06d", n), text))
      check(store.put("count", tostring(n)))
    end)
    return "note " .. n .. " saved"
  end,
}

-- notebook.list: every note, oldest first (keys sort by their bytes: note.000001, ...).
sco.register_command{
  name  = "notebook.list",
  title = "List notes",
  fn = function()
    local list = {}
    for _, key in ipairs(check(store.keys("note."))) do
      list[#list + 1] = check(store.get(key))
    end
    if #list == 0 then return notes(0) end
    return notes(#list) .. ": " .. table.concat(list, " | ")
  end,
}

-- notebook.clear: removes every note.
sco.register_command{
  name  = "notebook.clear",
  title = "Clear notes",
  fn = function()
    local keys = check(store.keys("note."))
    transaction(function()
      for _, key in ipairs(keys) do check(store.delete(key)) end
      check(store.delete("count"))
    end)
    return "removed " .. notes(#keys)
  end,
}

if store.available() then
  sco.status("Notebook ready")
else
  sco.log("warn", "no storage on this host: notes can't be kept")
end
