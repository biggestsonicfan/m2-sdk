-- lockstep/port.lua: the i960 port under MAME's Model 2 driver (sfight + tools/m2_load.lua).
-- A write tap on the i960's pac_frames catches every Pac-Man frame (also two in one Model 2
-- vblank): before frame n runs, frame n-1's RAM (pac_ram = 0x4000-0x4FFF) + pac_spr_xy ->
-- port.bin, every 60th frame's pac_fb -> port_NNNNN.fb, and frame n's inputs are written
-- into pac_in0/pac_in1 (the port's own input read has just run). Symbol addresses from
-- $LS_OUT/port.syms (`i960-elf-nm game.elf`).
local tools = os.getenv("LS_TOOLS") or "tools/lockstep"
dofile(tools .. "/../m2_load.lua")
if LS_PORT then return end                       -- the script re-runs after m2_load's reset
LS_PORT = true
dofile(tools .. "/inputs.lua")
local SYM = {}
for l in io.lines(LS_OUT .. "/port.syms") do
  local a, n = l:match("^(%x+) %a (%S+)$")
  if a then SYM[n] = tonumber(a, 16) end
end
local sp = manager.machine.devices[":maincpu"].spaces["program"]
local out = assert(io.open(LS_OUT .. "/port.bin", "wb"))
local RAM, XY, IN0, IN1, FB = SYM._pac_ram, SYM._pac_spr_xy, SYM._pac_in0, SYM._pac_in1, SYM._pac_fb
local done = false
LS_TAP = sp:install_write_tap(SYM._pac_frames, SYM._pac_frames + 3, "ls_frames", function(offset, data, mask)
  if done or data == 0 then return end
  local n = data
  if n > 1 then
    local t = {}
    for i = 0, 0xfff do t[#t + 1] = string.char(sp:read_u8(RAM + i)) end
    for i = 0, 15 do t[#t + 1] = string.char(sp:read_u8(XY + i)) end
    out:write(table.concat(t))
    if (n - 1) % 60 == 0 then
      local u = {}
      for i = 0, 1008 * 32 - 1 do u[#u + 1] = string.char(sp:read_u8(FB + i)) end
      local f = io.open(string.format("%s/port_%05d.fb", LS_OUT, n - 1), "wb")
      f:write(table.concat(u)); f:close()
    end
    if n - 1 >= LS_FRAMES then done = true; out:close(); manager.machine:exit(); return end
  end
  local a, b = LS_INPUTS(n)
  sp:write_u8(IN0, a); sp:write_u8(IN1, b)
end)
