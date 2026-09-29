-- m2_speed.lua: print MAME's own speed every ~5 s (an -autoboot_script). Below 100% the
-- host can't keep up and the sound breaks up; the game's SPEED panel can't show that (it
-- counts emulated frames against emulated vblanks).
local frames = 0
M2_SPEED = emu.add_machine_frame_notifier(function()
  frames = frames + 1
  if frames % 300 == 0 then
    print(string.format("MAME speed %.0f%%", manager.machine.video.speed_percent * 100))
  end
end)
