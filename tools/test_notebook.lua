-- SPDX-License-Identifier: GPL-3.0-only
-- Execute the shipped example against a transaction-aware synthetic store.
local commands, values, saved = {}, {}, nil
local fail_delete, fail_commit, fail_rollback, deletes = nil, false, false, 0
local function copy(src)
  local out = {}
  for k, v in pairs(src) do out[k] = v end
  return out
end
local st = {}
function st.available() return true end
function st.get(k) return values[k] end
function st.put(k, v) values[k] = v; return true end
function st.keys(prefix)
  local out = {}
  for k in pairs(values) do if k:sub(1, #prefix) == prefix then out[#out + 1] = k end end
  table.sort(out)
  return out
end
function st.delete(k)
  deletes = deletes + 1
  if deletes == fail_delete then return nil, "failed", "fixture delete failure" end
  local had = values[k] ~= nil
  values[k] = nil
  return had
end
function st.begin()
  if saved then return nil, "bad_arg", "transaction already open" end
  saved = copy(values)
  return true
end
function st.commit()
  if fail_commit then return nil, "failed", "fixture commit failure" end
  saved = nil
  return true
end
function st.rollback()
  if fail_rollback then return nil, "failed", "fixture rollback failure" end
  values, saved = saved, nil
  return true
end
sco = {store = st, register_command = function(c) commands[c.name] = c.fn end,
       status = function() end, log = function() end}
dofile(assert(arg[1], "pass notebook/main.lua"))
local add, clear, list = commands['notebook.add'], commands['notebook.clear'], commands['notebook.list']
assert(add('one') == 'note 1 saved' and add('two') == 'note 2 saved')
fail_delete, deletes = 2, 0
local ok, err = pcall(clear)
assert(not ok and err:find('fixture delete failure', 1, true))
assert(saved == nil and list() == '2 notes: one | two')
fail_delete, fail_commit = nil, true
ok, err = pcall(function() add('three') end)
assert(not ok and err:find('fixture commit failure', 1, true))
assert(saved == nil and list() == '2 notes: one | two')
fail_commit, fail_delete, fail_rollback, deletes = false, 1, true, 0
ok, err = pcall(clear)
assert(not ok and err:find('fixture delete failure', 1, true) and err:find('fixture rollback failure', 1, true))
-- Reset the synthetic fault before continuing; a real failed rollback needs host cleanup.
fail_delete, fail_rollback = nil, false
assert(st.rollback())
assert(clear() == 'removed 2 notes' and list() == '0 notes')
assert(clear() == 'removed 0 notes') -- delete of missing count returns false, not an error
print('notebook transaction failure tests: OK')
