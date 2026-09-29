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
emu.register_frame_done(function()
  frame = frame + 1
  local t = {}
  for a = 0x4000, 0x4fff do t[#t + 1] = string.char(sp:read_u8(a)) end
  local s2 = shares[":spriteram2"]
  for i = 0, 15 do t[#t + 1] = string.char(s2 and s2:read_u8(i) or 0) end
  out:write(table.concat(t))
  -- the screen now shows the picture taken at the previous vblank: frame - 1's
  if (frame - 1) % 60 == 0 and frame > 1 then
    local f = io.open(string.format("%s/ref_%05d.raw", LS_OUT, frame - 1), "wb")
    f:write((manager.machine.screens[":screen"]:pixels())); f:close()
  end
  cur0, cur1 = LS_INPUTS(frame + 1)
  if frame >= LS_FRAMES + 1 then out:close(); manager.machine:exit() end
end)
