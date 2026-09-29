-- lockstep/port.lua: the i960 port under MAME's Model 2 driver: sfight with the three EPROMs
-- swapped (run.sh EPROMS=), or a stock sfight + tools/m2_load.lua (M2_GAME_BIN set).
-- A write tap on the i960's pac_frames catches every Pac-Man frame (also two in one Model 2
-- vblank): before frame n runs, frame n-1's RAM (pac_ram = 0x4000-0x4FFF) + pac_spr_xy ->
-- port.bin, every 60th frame's pac_fb -> port_NNNNN.fb, and frame n's inputs are written
-- into pac_in0/pac_in1 (the port's own input read has just run). Symbol addresses from
-- $LS_OUT/port.syms (`i960-elf-nm game.elf`).
local tools = os.getenv("LS_TOOLS") or "tools/lockstep"
if os.getenv("M2_GAME_BIN") then dofile(tools .. "/../m2_load.lua") end
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
-- the same per-frame state record as ref.lua's (compare.py LS_STATE), read from the port's
-- own variables (z80_t: r[8] B C D E H L F A, alt[8], ix iy sp pc, i rr iff1 iff2 im, irq_vec,
-- r7, ..., halted at +33), plus the SCSP's three slots as the i960 has set them (68000 0x100000)
local Z = SYM._z80
local snd = manager.machine.devices[":audiocpu"].spaces["program"]
local sout = assert(io.open(LS_OUT .. "/port_state.bin", "wb"))
-- every write the sound board's 68000 makes to SCSP slots 0-2 (port_scsp.bin: time f64,
-- register offset u16, value u16), so compare.py can match each change the WSG asks for
local wout = assert(io.open(LS_OUT .. "/port_scsp.bin", "wb"))
LS_SCSP = snd:install_write_tap(0x100000, 0x10005f, "ls_scsp", function(offset, data, mask)
  if not done then wout:write(string.pack("<dI2I2", manager.machine.time:as_double(), offset - 0x100000, data & 0xffff)) end
end)
local u8 = function(a) return sp:read_u8(a) end
local function regs(Z)
  local t = {}
  for _, i in ipairs({ 7, 6, 0, 1, 2, 3, 4, 5 }) do t[#t + 1] = string.char(u8(Z + i)) end
  for _, i in ipairs({ 7, 6, 0, 1, 2, 3, 4, 5 }) do t[#t + 1] = string.char(u8(Z + 8 + i)) end
  for i = 16, 23 do t[#t + 1] = string.char(u8(Z + i)) end
  local r = (u8(Z + 30) & 0x80) | (u8(Z + 25) & 0x7f)          -- R: bit 7 (r7) + the M1 count (rr)
  t[#t + 1] = string.char(u8(Z + 24), r, u8(Z + 28), u8(Z + 26), u8(Z + 27), u8(Z + 33))
  return table.concat(t)
end
-- every accepted IRQ (port_irq.bin, as ref_irq.bin): the port's Z80_IRQ_HOOK snapshot
local iout = assert(io.open(LS_OUT .. "/port_irq.bin", "wb"))
local irqs = 0
local function state()
  local t = { string.pack("<d", manager.machine.time:as_double()), regs(Z) }
  t[#t + 1] = string.char(u8(SYM._pac_irq_mask), u8(SYM._pac_vector), u8(SYM._pac_latch), u8(SYM._pac_snd_on))
  for i = 0, 31 do t[#t + 1] = string.char(u8(SYM._pac_snd + i)) end
  for v = 0, 2 do
    for _, o in ipairs({ 0x00, 0x02, 0x0C, 0x10 }) do t[#t + 1] = string.pack("<I2", snd:read_u16(0x100000 + v * 0x20 + o)) end
  end
  return table.concat(t)
end
LS_TAP = sp:install_write_tap(SYM._pac_frames, SYM._pac_frames + 3, "ls_frames", function(offset, data, mask)
  if done or data == 0 then return end
  local n = data
  if n > 1 then
    local t = {}
    for i = 0, 0xfff do t[#t + 1] = string.char(sp:read_u8(RAM + i)) end
    for i = 0, 15 do t[#t + 1] = string.char(sp:read_u8(XY + i)) end
    out:write(table.concat(t))
    sout:write(state())
    local c = sp:read_u32(SYM._pac_irq_count)
    if c ~= irqs then
      if c - irqs > 1 then print(string.format("lockstep: frame %d took %d IRQs, 1 recorded", n - 1, c - irqs)) end
      irqs = c
      iout:write(string.pack("<I4", n - 1) .. regs(SYM._pac_irq_regs))
    end
    if (n - 1) % 60 == 0 then
      local u = {}
      for i = 0, 1008 * 32 - 1 do u[#u + 1] = string.char(sp:read_u8(FB + i)) end
      local f = io.open(string.format("%s/port_%05d.fb", LS_OUT, n - 1), "wb")
      f:write(table.concat(u)); f:close()
    end
    if n - 1 >= LS_FRAMES then done = true; out:close(); sout:close(); iout:close(); wout:close(); manager.machine:exit(); return end
  end
  local a, b = LS_INPUTS(n)
  sp:write_u8(IN0, a); sp:write_u8(IN1, b)
end)
