-- stftest_dump.lua - MAME -autoboot_script for src/stftest.c: once g_stf_done reaches the case
-- count (or at the frame limit), write g_stf_out to a file and quit. Read by tools/stftest_cmp.py.
-- Environment (the addresses are in the build's game.map):
--   STF_OUT    output file          STF_ADDR  address of _g_stf_out   STF_DONE  address of _g_stf_done
--   STF_NC     STF_NCASES           STF_NW    words to dump (STF_NOUT + 13*STF_NCASES + 64)
--   STF_FRAMES frame limit (default 3000)
local sp = manager.machine.devices[":maincpu"].spaces["program"]
local cpu = manager.machine.devices[":maincpu"]
local base, doneat = tonumber(os.getenv("STF_ADDR")), tonumber(os.getenv("STF_DONE"))
local nc, nw = tonumber(os.getenv("STF_NC")), tonumber(os.getenv("STF_NW"))
local lim = tonumber(os.getenv("STF_FRAMES") or "3000")
local n = 0
sub = emu.add_machine_frame_notifier(function()
  n = n + 1
  local done = sp:read_u32(doneat)
  if done >= nc or n >= lim then
    local f = io.open(os.getenv("STF_OUT"), "w")
    f:write(string.format("done %d frame %d pc %08X\n", done, n, cpu.state["CURPC"].value))
    for i = 0, nw - 1 do f:write(string.format("%08x\n", sp:read_u32(base + 4 * i))) end
    f:close()
    manager.machine:exit()
  end
end)
