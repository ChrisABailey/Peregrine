-- SPDX-License-Identifier: LGPL-3.0-or-later
-- Copyright (C) 2026 Chris Bailey
-- Part of Peregrine, a cross-platform port of FalconView(tm).
-- See COPYING.LESSER and NOTICE.md for the full licensing picture.

--- Runs a tilemaker profile's way_function over synthetic ways, without
--- tilemaker, and prints the tile attributes each way produces.
---
--- Usage: lua legend_way_harness.lua <profile.lua> <cases.lua>
--- `cases.lua` returns a list of {id=, tags={...}, relations={{...}, ...}}.
--- Output: one line per attribute, "<id>\t<layer>\t<key>\t<value>". A layer
--- with no attributes prints with an empty key.

local profile_path, cases_path = arg[1], arg[2]

local tags, relations, rel_cursor, out, case_id = {}, {}, 0, {}, ""
local current

local function emit_layer(name)
  current = {layer = name, attrs = {}}
  out[#out + 1] = current
end

local function set_attr(key, value)
  if current == nil then error("attribute '" .. key .. "' before any Layer()") end
  current.attrs[#current.attrs + 1] = {key, tostring(value)}
end

-- The tilemaker API the profile calls while processing a way.
function Find(key) return tags[key] or "" end
function Holds(key) return tags[key] ~= nil end
function Layer(name, is_area) emit_layer(name) end
function LayerAsCentroid(name) emit_layer(name) end
function Attribute(key, value) set_attr(key, value) end
function AttributeInteger(key, value) set_attr(key, math.floor(value)) end
function AttributeNumeric(key, value) set_attr(key, value) end
function AttributeBoolean(key, value) set_attr(key, value and "true" or "false") end
function MinZoom() end
function ZOrder() end
function IsClosed() return false end
function Area() return 0 end
function Length() return 100 end
function Id() return 1 end
function Accept() end
function RestartRelations() rel_cursor = 0 end
function NextRelation()
  rel_cursor = rel_cursor + 1
  if relations[rel_cursor] == nil then return nil end
  return rel_cursor
end
function FindInRelation(key)
  local r = relations[rel_cursor]
  return r and r[key] or ""
end

dofile(profile_path)

for _, case in ipairs(dofile(cases_path)) do
  case_id, tags, relations, rel_cursor, out = case.id, case.tags, case.relations or {}, 0, {}
  current = nil
  way_function()
  for _, l in ipairs(out) do
    if #l.attrs == 0 then print(case_id .. "\t" .. l.layer .. "\t\t") end
    for _, kv in ipairs(l.attrs) do
      print(case_id .. "\t" .. l.layer .. "\t" .. kv[1] .. "\t" .. kv[2])
    end
  end
end
