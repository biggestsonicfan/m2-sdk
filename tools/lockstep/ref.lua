-- lockstep/ref.lua: MAME's own pacman driver. After every frame: the Z80's RAM
-- 0x4000-0x4FFF + the sprite coordinates (0x5060-0x506F) -> ref.bin; every 60th frame's
-- picture -> ref_NNNNN.raw (native 288x224 BGRA). Inputs: what the Z80 reads at
-- IN0/IN1 is replaced by LS_INPUTS (a read tap: exact, no coin-impulse logic in between).
--   pacman pacman -rompath <dir with pacman.zip> -video none -sound none -nothrottle \
--     -skip_gameinfo -autoboot_script tools/lockstep/ref.lua     (LS_TOOLS, LS_OUT, LS_FRAMES)
dofile((os.getenv("LS_TOOLS") or "tools/lockstep") .. "/inputs.lua")
local sp = manager.machine.devices[":maincpu"].spaces["program"]
local frame = 0
local cur0, cur1 = LS_INPUTS(1)
local out = assert(io.open(LS_OUT .. "/ref.bin", "wb"))
LS_T0 = sp:install_read_tap(0x5000, 0x503f, "ls_in0", function(offset, data, mask) return cur0 end)
LS_T1 = sp:install_read_tap(0x5040, 0x507f, "ls_in1", function(offset, data, mask)
  if (offset & 0xffc0) == 0x5040 then return cur1 end return data end)
local shares = manager.machine.memory.shares
-- per frame, besides the RAM (state.bin, one LS_STATE-byte record, see compare.py):
-- machine time, the Z80's registers, the board's latch/IRQ mask/IM 2 vector, the WSG
local m = manager.machine
local cpu, st = m.devices[":maincpu"], m.devices[":maincpu"].state
local function item(dev, name) return emu.item(m.devices[dev].items["0/" .. name]) end
local I = { mask = item(":", "m_irq_mask"), vec = item(":", "m_interrupt_vector"), q = item(":mainlatch", "m_q"),
  en = item(":namco", "m_sound_enable"), regs = item(":namco", "m_soundregs"),
  freq = item(":namco", "m_channel_list.frequency"), vol = item(":namco", "m_channel_list.volume"),
  wave = item(":namco", "m_channel_list.waveform_select") }
local sout = assert(io.open(LS_OUT .. "/ref_state.bin", "wb"))
local function regs()
  local t = {}
  for _, r in ipairs({ "A", "F", "B", "C", "D", "E", "H", "L" }) do t[#t + 1] = string.char(st[r].value & 0xff) end
  for _, r in ipairs({ "AF2", "BC2", "DE2", "HL2" }) do t[#t + 1] = string.pack(">I2", st[r].value) end
  for _, r in ipairs({ "IX", "IY", "SP", "PC" }) do t[#t + 1] = string.pack("<I2", st[r].value) end
  for _, r in ipairs({ "I", "R", "IM", "IFF1", "IFF2", "HALT" }) do t[#t + 1] = string.char(st[r].value & 0xff) end
  return table.concat(t)
end
-- every accepted IRQ (ref_irq.bin: frame u32 + regs()): the Z80 has pushed PC and reads
-- the IM 2 vector word at I:vector -- a read tap on that word, (re)placed as I/vector change
local iout = assert(io.open(LS_OUT .. "/ref_irq.bin", "wb"))
local vaddr = -1
local function acked(offset)       -- this read is the acknowledge's vector read
  return offset == (((st.I.value & 0xff) << 8) | (I.vec:read(0) & 0xfe))
    and st.IFF1.value == 0 and st.IFF2.value == 0
    and (offset - st.PC.value) & 0xffff > 3        -- not the code at PC fetching it
end
local function watch_vector(vec)
  if vaddr < 0 then return end     -- still on the whole-ROM tap below
  local a = ((st.I.value & 0xff) << 8) | ((vec or I.vec:read(0)) & 0xfe)
  if a == vaddr then return end
  vaddr = a
  if LS_VT then LS_VT:remove() end
  LS_VT = sp:install_read_tap(a, a + 1, "ls_vec", function(offset, data, mask)
    if offset == vaddr and acked(offset) then iout:write(string.pack("<I4", frame + 1) .. regs()) end
  end)
end
-- until the first IRQ the game is still setting I and the vector: watch every ROM read
LS_VT = sp:install_read_tap(0x0000, 0x3fff, "ls_vec0", function(offset, data, mask)
  if vaddr < 0 and acked(offset) then
    iout:write(string.pack("<I4", frame + 1) .. regs())
    vaddr = 0; watch_vector()
  end
end)
-- OUT (any port) sets the vector: follow it at once (the next IRQ may come in that frame)
LS_OUT_TAP = cpu.spaces["io"]:install_write_tap(0x00, 0xff, "ls_out", function(offset, data, mask)
  watch_vector(data & 0xff)
end)
local function state()
  local t = { string.pack("<d", m.time:as_double()), regs() }
  t[#t + 1] = string.char(I.mask:read(0), I.vec:read(0), I.q:read(0), I.en:read(0))
  for i = 0, 31 do t[#t + 1] = string.char(I.regs:read(i)) end
  for v = 0, 2 do t[#t + 1] = string.pack("<I4I4I2", I.freq:read(v), I.vol:read(v * 4), I.wave:read(v)) end
  return table.concat(t)
end
emu.register_frame_done(function()
  frame = frame + 1
  local t = {}
  for a = 0x4000, 0x4fff do t[#t + 1] = string.char(sp:read_u8(a)) end
  local s2 = shares[":spriteram2"]
  for i = 0, 15 do t[#t + 1] = string.char(s2 and s2:read_u8(i) or 0) end
  out:write(table.concat(t))
  sout:write(state())
  watch_vector()
  -- the screen now shows the picture taken at the previous vblank: frame - 1's
  if (frame - 1) % 60 == 0 and frame > 1 then
    local f = io.open(string.format("%s/ref_%05d.raw", LS_OUT, frame - 1), "wb")
    f:write((manager.machine.screens[":screen"]:pixels())); f:close()
  end
  cur0, cur1 = LS_INPUTS(frame + 1)
  if frame >= LS_FRAMES + 1 then out:close(); sout:close(); iout:close(); manager.machine:exit() end
end)
