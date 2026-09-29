-- lockstep/inputs.lua: the input script both recorders play (Pac-Man IN0/IN1, active low,
-- for frame k, 1-based): a coin and a start every 3000 frames, a new stick direction
-- every 17 frames from a fixed LCG. LS_FRAMES frames; files go to LS_OUT.
local function lcg(n)
  local x = 12345
  for _ = 1, (n % 64) + 1 do x = (x * 1103515245 + 12345) & 0xffffffff end
  return x
end
function LS_INPUTS(k)
  local in0, in1 = 0xff, 0xff
  local m = k % 3000
  if m >= 700 and m < 706 then in0 = in0 & ~0x20 end      -- coin 1
  if m >= 760 and m < 766 then in1 = in1 & ~0x20 end      -- 1 player start
  local dir = (lcg(k // 17 + (k // 3000) * 7) >> 16) & 3  -- up/left/right/down
  in0 = in0 & ~(1 << dir)
  return in0, in1
end
LS_FRAMES = tonumber(os.getenv("LS_FRAMES") or "3000")
LS_OUT = os.getenv("LS_OUT") or "/tmp/lockstep"
