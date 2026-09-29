-- m2_load.lua — run an m2-sdk program on a stock sfight romset, no ROM swapping.
--
--   mame sfight -autoboot_script tools/m2_load.lua        (M2_GAME_BIN=path/to/game.bin)
--
-- Copies the flat program image (the build's game.bin, also written to
-- roms/<game>/game.bin) over the :maincpu program ROM region, then soft-resets, so the
-- i960 boots it. Default path /files/game.bin is where Pinboard's web MAME puts
-- launch `files`.
--
-- Optionally also replaces the sound board's 68000 program (:audiocpu, 0x600000) with a
-- raw big-endian 68000 image: $M2_SOUND_BIN, else /files/scsp_passthru.bin if present
-- (snd/scsp_passthru.bin, which lets the i960 drive the SCSP: src/m2_scsp.h).
local function getenv(k) return os.getenv and os.getenv(k) or nil end

local function slurp(path, required)
  local f = io.open(path, "rb")
  if not f then
    assert(not required, "m2_load: cannot open " .. path)
    return nil
  end
  local d = f:read("a")
  f:close()
  return d
end

local regions = manager.machine.memory.regions
local changed = false

-- i960 program: little-endian words
local path = getenv("M2_GAME_BIN") or "/files/game.bin"
local data = slurp(path, true)
local rgn = assert(regions[":maincpu"], "m2_load: no :maincpu region")
local n = math.min(#data, rgn.size) & ~3
-- MAME runs the autoboot script again after the reset: only load (and reset) once
for i = 1, n, 4 do
  if rgn:read_u32(i - 1) ~= string.unpack("<I4", data, i) then changed = true; break end
end
if changed then
  for i = 1, n, 4 do rgn:write_u32(i - 1, string.unpack("<I4", data, i)) end
  print(string.format("m2_load: %s, %d bytes -> :maincpu", path, n))
end

-- 68000 sound program: big-endian words; checked through the 68000's own view of it
local spath = getenv("M2_SOUND_BIN") or "/files/scsp_passthru.bin"
local sdata = slurp(spath, getenv("M2_SOUND_BIN") ~= nil)
local srgn = regions[":audiocpu"]
if sdata and srgn then
  local space = manager.machine.devices[":audiocpu"].spaces["program"]
  local sn = math.min(#sdata, srgn.size) & ~1
  local same = true
  for i = 1, sn, 2 do
    if space:read_u16(0x600000 + i - 1) ~= string.unpack(">I2", sdata, i) then same = false; break end
  end
  if not same then
    for i = 1, sn, 2 do srgn:write_u16(i - 1, string.unpack(">I2", sdata, i)) end
    assert(space:read_u16(0x600000) == string.unpack(">I2", sdata, 1),
           "m2_load: :audiocpu region byte order is not what the 68000 reads")
    print(string.format("m2_load: %s, %d bytes -> :audiocpu", spath, sn))
    changed = true
  end
end

if changed then manager.machine:soft_reset() end
