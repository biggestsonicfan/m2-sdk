-- vaporbench_dump.lua - MAME -autoboot_script for src/vaporbench.c: once every ext case has run
-- (g_vb_ext_done >= VB_NEXT) and every native command has been timed, write the results to a
-- file and quit. tools/vaporbench_cmp.py puts the boards' files side by side.
-- VB_EXTW (48) words a case in g_vb_ext_out, as in src/vaporbench.c.
-- Environment:
--   VB_MAP     the build's game.map (the g_vb_* addresses are read from it)
--   VB_OUT     output file
--   VB_FRAMES  frame limit (default 6000)
local sp = manager.machine.devices[":maincpu"].spaces["program"]
local names = { "FADD", "FSUB", "FMUL", "FDIV", "I2F", "F2I", "SQRT", "SIN", "COS", "TAN",
                "SINS", "COSS", "ATN2", "DST2", "DST3", "MAG2", "DOT2", "ADD3", "SUB3", "SCL3",
                "IDNT", "SPOS", "SCAL", "ANGX", "ANGY", "ANGZ", "RMAT", "WMAT", "PSPP", "SUBM" }
local sym = {}
for line in io.lines(os.getenv("VB_MAP")) do
  local a, s = line:match("^%s+0x(%x+)%s+_(g_vb_[%w_]+)%s*$")
  if a then sym[s] = tonumber(a, 16) end
end
local function rd(s, i) return sp:read_u32(sym[s] + 4 * (i or 0)) end
local lim = tonumber(os.getenv("VB_FRAMES") or "6000")
local n = 0
sub = emu.add_machine_frame_notifier(function()
  n = n + 1
  -- RAM holds anything until the program's boot has set it up: wait for sane counts
  local nx, fr = rd("g_vb_next"), rd("g_vb_frames")
  local ready = nx > 0 and nx < 1024 and fr < 0x1000000
  if (ready and rd("g_vb_ext_done") >= nx and fr >= 2 * #names) or n >= lim then
    local f = io.open(os.getenv("VB_OUT"), "w")
    f:write(string.format("frames %d ext_done %d scene_us %d objects %d frame_us %d\n",
      rd("g_vb_frames"), rd("g_vb_ext_done"), rd("g_vb_scene_us"), rd("g_vb_objs"), rd("g_vb_frame_us")))
    local bad = rd("g_vb_nat_bad")
    for i = 1, #names do
      f:write(string.format("nat %s %d %s\n", names[i], rd("g_vb_nat_ns", i - 1),
        ((bad >> (i - 1)) & 1) == 1 and "BAD" or "ok"))
    end
    -- per ext case: command, ns, then the words it sent (g_vb_ext_out, VB_EXTW words a case)
    local w = 48   -- VB_EXTW
    for i = 0, nx - 1 do
      local o = sym.g_vb_ext_out + 4 * w * i
      local line = { string.format("ext %02x %d", rd("g_vb_ext_cmd", i), rd("g_vb_ext_ns", i)) }
      for j = 1, sp:read_u32(o) do line[#line + 1] = string.format("%08x", sp:read_u32(o + 4 * j)) end
      f:write(table.concat(line, " ") .. "\n")
    end
    f:close()
    manager.machine:exit()
  end
end)
