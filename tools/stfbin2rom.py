import argparse, sys, array, os, struct, pathlib, hashlib
from functools import partial
from shutil import move

parser = argparse.ArgumentParser(description='Sonic the Fighters binary ROM parser')
parser.add_argument('--input', dest='input', type=argparse.FileType('rb', 0), help='[REQUIRED] File location of the ROM binary', required=True)
parser.add_argument('--output', dest='output', type=str, help='[Optional] Output directory for split ROMs (Input directory used by default)')
parser.add_argument('--verify', dest='verify', action=argparse.BooleanOptionalAction, help='[Optional] Use MD5 checksum verification on output (uses \"sfight.md5\" in script directory)')
parser.add_argument('--ctools', dest='win', action=argparse.BooleanOptionalAction, help='[Optional] Handle stupid ctools stuff')
parser.add_argument('--names', dest='names', nargs=2, metavar=('LO', 'HI'), default=['epr-19001.15', 'epr-19002.16'], help='[Optional] Output file names of the two EPROMs (default: sfight\'s epr-19001.15 epr-19002.16)')
parser.add_argument('--size', dest='size', type=lambda v: int(v, 0), default=None, help='[Optional] Program image size (default 0x100000). With --size, a bigger image is an error, not truncated')
parser.add_argument('--nopad', dest='nopad', action=argparse.BooleanOptionalAction, help='[Optional] Do NOT pad the ROM binary to 0x100000 with 0xFF (split as-is)')

def SizeRom(rom, win, nopad=False, size=None):
    print("Checking ROM size for splitting...")
    if win:
        with open(rom.name, 'rb') as ctools_file:
            with open(rom.name + "-fix", 'wb') as out_file:
                out_file.write(ctools_file.read()[int("0x2c", 16):])
        move(rom.name + "-fix", rom.name)
    rom_size = os.path.getsize(rom.name)
    max_size = size if size is not None else int("0x100000", 16)
    if size is not None and rom_size > max_size:
        sys.exit("Error: ROM binary is " + hex(rom_size) + " bytes, " + hex(rom_size - max_size) + " over the board's " + hex(max_size))
    if nopad:
        print("--nopad: leaving ROM binary unpadded at " + str(hex(rom_size)))
        return
    if rom_size < max_size:
        size_diff = max_size - rom_size
        print("ROM binary too small at " + str(hex(rom_size)) + " by " + str(hex(size_diff)) + " bytes, filling with 0xFF")
        with open(rom.name, "ab") as too_small:
            for count in range(0,size_diff):
                too_small.write(b'\xff')
        print("Size should be correct now: " + str(hex(os.path.getsize(rom.name))))
    elif rom_size > max_size:
        size_diff = rom_size - max_size
        print("ROM binary too big at " + str(hex(rom_size)) + " by " + str(hex(size_diff)) + " bytes, truncating...")
        with open(rom.name, "ab") as too_big:
            too_big.truncate(max_size)
        print("Size should be correct now: " + str(hex(os.path.getsize(rom.name))))
    else:
        print("... correct: " + str(hex(os.path.getsize(rom.name))))
        
def main():        
    try:
        args = parser.parse_args()
        SizeRom(args.input, args.win, args.nopad, args.size)
        output_dir = pathlib.Path(str(args.input.name)).parent.resolve()
        if args.output != None:
            if os.path.isdir(args.output) == True:
                output_dir = args.output
            else:
                print("Error: Output path \"" + args.output + "\" is not a valid path. Using input directory")
        try:
            if os.path.isfile(args.input.name) == False:
                raise FileNotFoundError
            
            output_files = args.names
            with args.input as rom, open(os.path.join(output_dir, output_files[0]), 'wb') as epr15, open(os.path.join(output_dir, output_files[1]), 'wb') as epr16:
                print("Attempting to create split ROM")
                for chunk1 in iter(partial(rom.read, 2), b''):
                    epr15.write(chunk1)
                    for chunk2 in iter(partial(rom.read, 2), b''):
                        epr16.write(chunk2)
                        break
                print("ROM files successfully split:\n" + epr15.name + "\n" + epr16.name)
                    
            if args.verify:
                with open("sfight.md5") as md5:
                    checksums = md5.readlines()
                    for i in range(2):
                        split_sum = checksums[i].split('  ')
                        if split_sum[1].replace('\n','') == output_files[i]:
                            with open(os.path.join(output_dir, output_files[i]), 'rb') as file_to_check:
                                data = file_to_check.read()
                                md5_returned = hashlib.md5(data).hexdigest()
                            if split_sum[0] == md5_returned:
                                print(output_files[i] + " checksum PASS")
                            else:
                                print(output_files[i] + " checksum FAIL")
                
        except FileNotFoundError:
           print("Error: Input file \"" + args.input + "\" not found.")
    except Exception:   # not SystemExit: an oversize image (--size) must fail the build
        parser.print_help()  

if __name__ == '__main__':
    main()
