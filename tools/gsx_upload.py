"""gsx_upload.py: run an SDK program built as a geoserial app (-DM2_GSX=ON, roms/<game>.gsx)
on a Model 2B with m2-x11's geoserial kernel burned: upload it, launch it, print what it
sends back over the link.

    /usr/bin/python3 tools/gsx_upload.py /dev/ttyUSB0 roms/vaporbench.gsx    real board
    python tools/gsx_upload.py COM5 roms/vaporbench.gsx
    python3 tools/gsx_upload.py --mame --rompath "<kernel dir>;<stock roms>" roms/vaporbench.gsx
    /usr/bin/python3 tools/gsx_upload.py /dev/ttyUSB0 --exit     back to the manager (PROG_EXIT)

It uses m2-x11's host driver (tools/geo_serial.py; --m2x11 if it isn't at ../m2-x11):
PING, delete an earlier upload of the same name (other programs stay), upload, check the
stored copy's pair-sum, launch, then print the app's text until it sends "<name>: done"
(or --wait seconds pass). The app keeps running until PROG_EXIT (--exit).

The board's receiver needs ~3 ms between bytes (--pace); a 40 KB app takes about 2 min.
"""
import argparse, asyncio, os, socket, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def load_geo_serial(m2x11):
    sys.path.insert(0, os.path.join(m2x11, "tools"))
    import geo_serial
    return geo_serial


async def read_report(gs, done, seconds):
    """Collect the app's text (gs_puts) until its `done` line or `seconds` pass."""
    text, t0 = bytearray(gs.rx), time.time()    # what came in with the LAUNCH reply
    gs.rx.clear()
    while time.time() - t0 < seconds:
        await asyncio.sleep(0.1)
        text += await gs.link.recv_available()
        i = text.find(done)
        if i >= 0 and b"\n" in text[i:]:
            break
    return text.decode("latin-1").replace("\r", "")


async def run(gs, g, gsx, name, report_s):
    if not await gs.ping():
        ok, attempts, heard, statuses = await gs.wait_ping(25)
        if not ok:
            print(f"no PING reply ({attempts} tries, {heard} bytes heard, {statuses})")
            return None
    print("board: geoserial answers (GS01)")
    for slot, sname, size in await gs.prog_list():
        if sname == name:
            rep = await gs.prog_delete(slot)
            print(f"   deleted the earlier '{sname}' in slot {slot}: "
                  f"{'ok' if rep and rep[0] == g.GS_OK else rep}")
    t0 = time.time()
    slot = await gs.upload(gsx, name)
    if slot is None:
        return None
    print(f"uploaded {os.path.getsize(gsx)} bytes in {time.time() - t0:.0f} s")
    rep = await gs.prog_launch(slot)
    if not rep or rep[0] != g.GS_OK:
        print(f"LAUNCH refused: {rep}")
        return None
    print(f"launched '{name}' (slot {slot}); what it sends:")
    text = await read_report(gs, f"{name}: done".encode(), report_s)
    print("\n".join("   " + l for l in text.strip().splitlines()) or "   (no text)")
    if f"{name}: done" in text:
        return "done"
    print(f"no '{name}: done' line: check the screen")
    return None


async def main_serial(g, port, baud, pace, gsx, name, report_s, exit_only):
    link = g.SerialLink(port, baud, send_pace=pace)
    try:
        gs = g.GeoSerial(link)
        if exit_only:
            rep = await gs.prog_exit()
            print(f"PROG_EXIT -> {rep}")
            return "done" if rep and rep[0] == g.GS_OK else None
        return await run(gs, g, gsx, name, report_s)
    finally:
        link.close()


async def main_mame(g, gsx, name, rompath, report_s, snap_dir, linger):
    """MAME: claude_mame's bridge drives the emulated 315-5649. MAME_EXE_NAME (an absolute
    path works) picks the build; rompath must put the geoserial kernel's EPROM pair (a
    folder named sfight) before the stock sfight, schamp and segabill zips."""
    with socket.socket() as s:                  # a free port: other MAMEs may hold 6510
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
    b = g._MameBridge()(port=port)
    args = ["-rompath", rompath, "-debugger", "none", "-video", "none", "-sound", "none",
            "-nothrottle"]
    if snap_dir:
        args += ["-snapshot_directory", snap_dir, "-snapview", "native", "-snapsize", "auto"]
    await b.launch_mame("sfight", args)
    try:
        await b.call("continue")
        await asyncio.sleep(4.0)
        gs = g.GeoSerial(g.MameLink(b))
        await gs.drain_boot_log()
        res = await run(gs, g, gsx, name, report_s)
        await asyncio.sleep(linger)
        if snap_dir:
            await b.call("snapshot")
            await asyncio.sleep(0.5)
        if res:
            rep = await gs.prog_exit()
            back = await gs.ping()
            print(f"PROG_EXIT -> {rep}; the manager {'answers' if back else 'does NOT answer'}")
            if snap_dir:
                await asyncio.sleep(1.0)
                await b.call("snapshot")
                await asyncio.sleep(0.5)
            if not back:
                res = None
        if snap_dir:
            print(f"snapshots in {snap_dir}")
        return res
    finally:
        await b.shutdown_mame()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("port", nargs="?", help="serial port of the RS-422 adapter (COM5)")
    ap.add_argument("gsx", nargs="?", default=os.path.join(ROOT, "roms", "vaporbench.gsx"))
    ap.add_argument("--name", help="name in the kernel's store (default: the file's stem)")
    ap.add_argument("--baud", type=int, default=2_000_000)
    ap.add_argument("--pace", type=float, default=0.003,
                    help="seconds between bytes sent (the 315-5649 needs ~3 ms)")
    ap.add_argument("--m2x11", default=os.path.join(ROOT, "..", "m2-x11"))
    ap.add_argument("--exit", action="store_true", help="PROG_EXIT: back to the manager")
    ap.add_argument("--wait", type=float, default=30, help="seconds to wait for the report")
    ap.add_argument("--mame", action="store_true", help="test on MAME instead of a board")
    ap.add_argument("--rompath", default="", help="MAME: rompath (kernel pair first)")
    ap.add_argument("--snap", default="", help="MAME: snapshots (running, back) here")
    ap.add_argument("--linger", type=float, default=0, help="MAME: seconds to run after the report")
    a = ap.parse_args()
    if not a.mame and not a.port:
        ap.error("give the serial port (COM5), or --mame")
    name = a.name or os.path.splitext(os.path.basename(a.gsx))[0]
    g = load_geo_serial(a.m2x11)
    if a.mame:
        res = asyncio.run(main_mame(g, a.gsx, name, a.rompath, a.wait, a.snap, a.linger))
    else:
        res = asyncio.run(main_serial(g, a.port, a.baud, a.pace, a.gsx, name, a.wait, a.exit))
    sys.exit(0 if res else 1)


if __name__ == "__main__":
    main()
