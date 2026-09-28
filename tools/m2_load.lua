-- m2_load.lua — run an m2-sdk program on a stock sfight romset, no ROM swapping.
--
--   mame sfight -autoboot_script tools/m2_load.lua        (M2_GAME_BIN=path/to/game.bin)
--
-- Copies the flat program image (the build's game.bin, also written to
-- roms/<game>/game.bin) over the :maincpu program ROM region, then soft-resets, so the
-- i960 boots it. Default path /files/game.bin is where Pinboard's web MAME puts
-- launch `files`.
local path = os.getenv and os.getenv("M2_GAME_BIN") or nil
path = path or "/files/game.bin"
local f = assert(io.open(path, "rb"), "m2_load: cannot open " .. path)
local data = f:read("a")
f:close()
local rgn = assert(manager.machine.memory.regions[":maincpu"], "m2_load: no :maincpu region")
local n = math.min(#data, rgn.size) & ~3
-- MAME runs the autoboot script again after the reset: only load (and reset) once
local same = true
for i = 1, n, 4 do
  if rgn:read_u32(i - 1) ~= string.unpack("<I4", data, i) then same = false; break end
end
if not same then
  for i = 1, n, 4 do
    rgn:write_u32(i - 1, string.unpack("<I4", data, i))
  end
  print(string.format("m2_load: %s, %d bytes -> :maincpu", path, n))
  manager.machine:soft_reset()
end
