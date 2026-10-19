-- copext_check.lua - MAME -autoboot_script for src/meshcube_ext.c (canaries model2/m2-sdk-2a-copext,
-- -2c-copext): after CE_FRAMES frames (default 300), write one line to CE_OUT and quit:
--   mark <buffer RAM word 0x4000> copext <g_copext> ans <g_copext_ans> PASS|FAIL
-- PASS needs the VM mark "M2VM" (0x4d32564d) at word 0x4000 and g_copext = 7 (mark seen, 0x19
-- answered, answer right; see meshcube.c). CE_MAP is the build's game.map; without it (an older
-- build) only the mark is read. CE_SNAP set: also save a snapshot.
local sp = manager.machine.devices[":maincpu"].spaces["program"]
local sym = {}
local map = os.getenv("CE_MAP")
if map and map ~= "" then
  for line in io.lines(map) do
    local a, s = line:match("^%s+0x(%x+)%s+_(g_copext[%w_]*)%s*$")
    if a then sym[s] = tonumber(a, 16) end
  end
end
local function rd(s) return sym[s] and sp:read_u32(sym[s]) or 0 end
local lim = tonumber(os.getenv("CE_FRAMES") or "300")
local n = 0
sub = emu.add_machine_frame_notifier(function()
  n = n + 1
  if n < lim then return end
  local mark = sp:read_u32(0x900000 + 4 * 0x4000)
  local ok = mark == 0x4d32564d and (not sym.g_copext or rd("g_copext") == 7)
  local f = io.open(os.getenv("CE_OUT"), "w")
  f:write(string.format("mark %08x copext %d ans %08x %s\n", mark, rd("g_copext"), rd("g_copext_ans"),
    ok and "PASS" or "FAIL"))
  f:close()
  if os.getenv("CE_SNAP") then manager.machine.video:snapshot() end
  manager.machine:exit()
end)
