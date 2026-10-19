-- inputtest_check.lua: MAME -autoboot_script for src/inputtest.c. Holds IN0's START1 from
-- frame IT_PRESS to IT_PRESS+60 and reads g_in_raw / g_in_dec (addresses from the build's
-- game.map) before and while it is held. The program must see START1 only while it is held.
--   IT_MAP     the build's game.map
--   IT_OUT     output file: "idle raw %06x dec %06x  held raw %06x dec %06x PASS|FAIL"
--   IT_PRESS   frame to press START1 (default 200)
local addr = {}
for line in io.lines(os.getenv("IT_MAP")) do
  local a, s = line:match("^%s+0x(%x+)%s+_(g_in_%w+)%s*$")
  if a then addr[s] = tonumber(a, 16) end
end
local press = tonumber(os.getenv("IT_PRESS") or "200")
local cpu = manager.machine.devices[":maincpu"].spaces["program"]
local start
for _, f in pairs(manager.machine.ioport.ports[":IN0"].fields) do
  if f.mask == 0x10 then start = f end
end
local n, idle_raw, idle_dec = 0, 0, 0
emu.register_frame_done(function()
  n = n + 1
  if n == press - 1 then
    idle_raw = cpu:read_u32(addr.g_in_raw); idle_dec = cpu:read_u32(addr.g_in_dec)
  elseif n == press then
    start:set_value(1)
  elseif n == press + 30 then
    local raw, dec = cpu:read_u32(addr.g_in_raw), cpu:read_u32(addr.g_in_dec)
    start:set_value(0)
    -- held: IN0 bit4 low, m2_start bit0 and m2_input(0) M2_START (0x80) set; idle: neither
    local ok = (idle_raw & 0x10) ~= 0 and (raw & 0x10) == 0
           and (idle_dec & 0x10080) == 0 and (dec & 0x10080) == 0x10080
           and cpu:read_u32(addr.g_in_frames) > press
    local f = io.open(os.getenv("IT_OUT"), "w")
    f:write(string.format("idle raw %06x dec %06x  held raw %06x dec %06x %s\n",
      idle_raw, idle_dec, raw, dec, ok and "PASS" or "FAIL"))
    f:close()
    manager.machine:exit()
  end
end)
