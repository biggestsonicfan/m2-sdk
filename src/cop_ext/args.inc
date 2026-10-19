/* From m2-hle2 (2662dda2) src/board/sharc_exec.h: the argument count of each cpres1 command. */
#define COP_ARGS_STREAM (-1)

static inline int sharc_args_for_cmd(uint32_t cmd) {
    switch (cmd) {
        /* Setters */
        case 0x03000606: return 3;
        case 0x03800707: return 3;
        case 0x04000808: return 1;
        case 0x04800909: return 1;
        case 0x05000A0A: return 1;
        /* Math: 2-in / 1-out */
        case 0x12002424: return 2;
        case 0x12802525: return 2;
        /* Math: 6-in / 3-out */
        case 0x2E005C5C: return 6;
        /* 0x1A003434: object→polygon index lookup — 1 arg, 1 result */
        case 0x1A003434: return 1;
        /* snc_eye_thd_set camera transforms */
        case 0x18003030: return 3;
        case 0x13002626: return 1;
        /* Animation curve interpolation: 6-in / 1-out */
        case 0x19003232: return 6;
        /* Afterimage (zanzou) — cpres1 PM 0x208E1..0x20A8E, sharc_zanzou.h.
         * 0x80 is variable-length; cop.h streams it (COP_ARGS_STREAM). */
        case 0x40008080: return COP_ARGS_STREAM;
        case 0x42808585: return 1;
        case 0x42008484: return 1;
        case 0x41808383: return 1;
        case 0x43808787: return 1;
        /* Shadow slot / matrix commands */
        case 0x39807373: return 2;
        case 0x3A807575: return 6;
        case 0x3A007474: return 7;
        case 0x05800B0B: return 12;
        case 0x23004646: return 1;
        /* COP internal memory (collision/afterimage data) */
        case 0x24004848: return 1;
        case 0x24804949: return 2;
        case 0x43008686: return 1;
        case 0x09801313: return 2;  /* fadd(a,b) → float  MAME-verified PM 0x0205B0 */
        case 0x0A001414: return 2;
        case 0x0A801515: return 2;
        case 0x0B001616: return 2;
        case 0x16802D2D: return 2;
        case 0x13802727: return 2;
        case 0x1B803737: return 2;
        case 0x1B003636: return 2;
        /* smooth_int: reset bone slot to identity + 9 ang ops (Z,Y,X,X,Y,Z,Y,Z,X) */
        case 0x2A005454: return 9;
        case 0x2A805555: return 9;  /* dispatch[0x55] PM 0x02115F — 9 args (3 triples) / 3 outputs. i960 sends opcode+3 stt then reads 3 (e.g. loop @0x2FB00). 0-arg desynced the FIFO. */
        case 0x08001010: return 0;  /* dispatch[0x10] PM 0x20460 — IDA: 0 args, 0 outputs (kira_kira_disp) */
        case 0x34006868: return 1;  /* dispatch[0x68] PM 0x205A3 — IDA: 1 arg, 0 outputs (name_char_kage_disp) */
        case 0x3B807777: return 4;  /* dispatch[0x77] PM 0x20B1F — IDA: 4 args (x,y,z,flags), 9 outputs */
        /* Collision / spatial — no FIFO output */
        case 0x1F003E3E: return 5;
        case 0x1D003A3A: return 4;
        case 0x1D803B3B: return 7;
        case 0x1E803D3D: return 6;
        case 0x39007272: return 3;
        case 0x38007070: return 5;
        case 0x38807171: return 2;  /* Fn_ball_to_unit: mask, player -> 1 */
        /* Math: 1-in / 1-out */
        case 0x10802121: return 1;
        case 0x11002222: return 1;
        case 0x0B801717: return 1;
        case 0x0C001818: return 1;
        /* Spatial: 4-in / 1-out */
        case 0x15802B2B: return 4;
        case 0x17802F2F: return 4;
        case 0x2F005E5E: return 4;
        /* Vector transform: 3-in / 3-out */
        case 0x14802929: return 3;
        case 0x35006A6A: return 3;
        /* Bone slot write/select/flush — no FIFO output */
        case 0x07000E0E: return 3;  /* Fn_load_point: T = the three floats */
        case 0x1A803535: return 2;  /* save 12 words from bone slot to selection buffer */
        case 0x34806969: return 2;  /* load animation frame + build rotation matrix in bone slot */
        case 0x1C803939: return 4;  /* normalize 3 float args, write to bone buffer at arg4 offset */
        case 0x08801111: return 9;
        case 0x33806767: return 1;
        case 0x31006262: return 9;
        case 0x3F807F7F: return 1;  /* flush selection buffer (192 words) to output buffer */
        case 0x1C003838: return 1;  /* select bone data buffer (arg=1 → player 2, else player 1) */
        /* get_rot_matrix: read current 3×3 → 9 results */
        case 0x09001212: return 0;
        /* calc_rob_angle_cont: 2-bone IK (4×stt + stl + 3×st = 17 args) → 1 result */
        case 0x35806B6B: return 17;
        /* get_frame_dat: frame data lookup (stq + stt = 7 args) → 3 results */
        case 0x31806363: return 7;
        /* Polygon submission: 8-in / 2-out */
        case 0x3C007878: return 8;
        /* os_set_coli commands */
        case 0x22004444: return 1;
        case 0x2B005656: return 0;
        case 0x2D005A5A: return 2;
        case 0x2D805B5B: return 3;
        case 0x2C805959: return 4;
        /* write matrix: 12 row-major floats → loads rot[] and pos[] */
        case 0x02000404: return 12;
        /* Zero-arg ops */
        case 0x11802323: return 1;  /* tan(i16 angle): sin/cos via sincos+RECIPS — PM 0x02061C */
        case 0x2B805757: return 0;  /* read col1 of rotation: rot[1][0..2] → 3 floats — PM 0x02044E */
        case 0x2C005858: return 0;  /* read col2 of rotation: rot[2][0..2] → 3 floats — PM 0x020457 */
        case 0x25004A4A: return 1;  /* animation data reader: type-dispatched — PM 0x02076E */
        case 0x02800505: return 0;
        case 0x07800F0F: return 0;
        case 0x06000C0C: return 0;
        case 0x00000000: return 0;
        case 0x00800101: return 0;
        case 0x01000202: return 0;
        case 0x01800303: return 0;
        case 0x06800D0D: return 0;
        case 0x40808181: return 0;
        case 0x41008282: return 0;
        case 0x1F803F3F: return 3;  /* set_ang_xyz: 3 i16 angle args, 1 zero returned */
        case 0x21804343: return 1;
        case 0x22804545: return 1;
        /* fvipers commands (arg counts verified from PM 0x020670/0x020E80/0x020EF5/0x0205AF) */
        case 0x16002C2C: return 6;  /* 3D distance: (x1,x2,y1,y2,z1,z2) → 1 float */
        case 0x17002E2E: return 3;  /* Fn_get_3d_len: |(x, y, z)| → 1 float */
        case 0x18803131: return 4;  /* interpolation: 4 floats → 1 float */
        case 0x28805151: return 0;  /* Fn_get_glo_ang: 3 angles of the current matrix */
        case 0x0D001A1A: return 1;  /* sqrt(arg0) → 1 float — PM 0x0205AF */
        default:         return 0;
    }
}
