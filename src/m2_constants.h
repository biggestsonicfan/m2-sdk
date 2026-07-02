/*
 * m2_constants.h — the Model 2B hardware map: bus addresses + device command encodings.
 *
 * Single authoritative home for every PLATFORM constant: coprocessor registers, command
 * FIFOs, the GEO command region + slot numbers, display-list opcodes, and the fixed RAM
 * regions (palette / colorxlat / luma / texram / BUFF_RAM). The rule:
 *   - a fact about the BOARD (or its shipped SHARC microcode) belongs HERE;
 *   - a fact about a GAME's data stays in the domain header that uses it
 *     (m2_geo.h GEO_MODEL_TABLE, m2_color.h M2_ROM_LUMA_*, m2_tex_codec.h STF_TEX_*,
 *      the captured STF_LIGHT_*_BITS in m2_geo.h, ...).
 *
 * Pure integer literals only — no typedefs required, safe to include first from any TU.
 * Deref/access macros (M2_COPFIFO, M2_GEOFIFO, M2_GEO_SLOT, M2_PALETTE, ...) stay with
 * their APIs in the domain headers.
 *
 * Provenance: STF/FV firmware disassembly (b_crx_copro_down, copro_down2, geo_initialize,
 * set_end_mark, cop_initialize, the cpres1/cpres2 SHARC disasm in ../m2-hle) + MAME
 * model2.cpp + silicon observation; entries note their source where it is not obvious.
 *
 * ⚠ SHARED ENCODING: the COP (cpres1) and GEO (cpres2) both use (n<<23)|(n<<8)|n command
 * words, so the same 32-bit value can be a COP op on the COP FIFO and a different GEO op
 * in a display list — e.g. 0x01800303 = COP_IDENTITY = GEO_OP_WINDOW, and 0x01000202 =
 * COP_POP = GEO_OP_DIRECT. ALWAYS write the named constant for the device you target.
 */
#ifndef M2_CONSTANTS_H
#define M2_CONSTANTS_H

/* ==== coprocessor boot/control registers ======================================================
 * cpres1 = COP/math, cpres2 = GEO/geometry — both ADSP-2106x SHARCs, host-booted (m2_3d.h). */
#define COP_CTL_REG       0x00980000u  /* cpres1 boot ctl: bit31 = halt + reset DMA counter        */
#define COP_STATUS_REG    0x00980004u  /* cpres1 status: bit0 = ready (STF cop_initialize polls it) */
#define GEO_BOOT_CTL_REG  0x00980008u  /* cpres2 boot ctl (same layout as COP_CTL_REG)             */
#define GEO_CTL_REG       0x0098000Cu  /* STF clears this in geo_initialize; low byte = the COP
                                        * poly bank cached by change_poly_bank @0x3534             */
#define COP_BOOTSTAT_REG  0x00980014u  /* observed: bit1 sets once cpres1 firmware is up (post-boot
                                        * poll); semantics otherwise undocumented                  */
#define COPRO_RESET_REG   0x00980020u  /* bits1:0 hold cpres1/2 in reset; clear to release.
                                        * UNMAPPED in MAME (the HLE never needed it) — real silicon
                                        * stays held in reset without the clear                    */
#define COP_IOP_BASE      0x008C0000u  /* cpres1 ADSP-2106x IOP register window                    */
#define GEO_IOP_BASE      0x00840000u  /* cpres2 ADSP-2106x IOP register window                    */

/* ==== command FIFOs + GEO pipeline registers ================================================== */
#define M2_COPFIFO_ADDR   0x00884000u  /* COP command FIFO (deref form: M2_COPFIFO, m2_math.h)     */
#define M2_GEOFIFO_ADDR   0x00804000u  /* GEO command FIFO (deref form: M2_GEOFIFO, m2_3d.h)       */
#define GEO_START         0x00800000u  /* g10 in STF — GEO command region (slot window base)       */
#define GEO_WRITE_REG     0x00801008u  /* geo_write_start (g10+0x1008)                             */
#define COP_WPOS_REG      0x00802008u  /* COP BUFF_RAM write cursor (read-only): polygon_submit's
                                        * commit position; frozen = the COP never committed        */
#define GEO_READ_REG      0x00803008u  /* write -> geo_read_start (commit a display list)          */
#define GEO_BUFFERRAM     0x00900000u  /* m_bufferram, i960-mapped (4 rotating 0x8000-byte buffers) */

/* ==== the shared SHARC command-word encoding ==================================================
 * BOTH coprocessors' command words are (N<<23)|(N<<8)|N — opcode in bits[28:23], with the
 * same byte repeated at [15:8] and [7:0] (the firmware checks all three fields). COP_CMD
 * and GEO_OP are the per-device spellings of the one encoding; the same 32-bit value can
 * therefore mean different ops on different devices (see the header warning above). */
#define M2_SHARC_CMD(n)  (((u32)(n) << 23) | ((u32)(n) << 8) | (u32)(n))

/* ==== COP (cpres1) command words ==============================================================
 * Op semantics verified against the cpres1 disassembly (../m2-hle). Angles are 16-bit fixed
 * point (0x10000 = 360deg). The scalar/vector wrappers live in m2_math.h. */
#define COP_CMD(n)    M2_SHARC_CMD(n)

#define COP_PUSH      COP_CMD(0x01)  /* push bone frame (STF name: "OBJECT")     */
#define COP_POP       COP_CMD(0x02)  /* pop bone frame  (STF name: "DIRECT")     */
#define COP_IDENTITY  COP_CMD(0x03)  /* rot = I, T = 0                          */
#define COP_ANG_X     COP_CMD(0x08)  /* in: 1 i16 angle; post-mul rot by Rx     */
#define COP_ANG_Y     COP_CMD(0x09)  /* in: 1 i16 angle; post-mul rot by Ry     */
#define COP_ANG_Z     COP_CMD(0x0A)  /* in: 1 i16 angle; post-mul rot by Rz     */
#define COP_FADD      COP_CMD(0x13)  /* in: a,b float; out: a+b                 */
#define COP_FSUB      COP_CMD(0x14)  /* in: a,b float; out: a-b                 */
#define COP_FMUL      COP_CMD(0x15)  /* in: a,b float; out: a*b                 */
#define COP_FDIV      COP_CMD(0x16)  /* in: a,b float; out: a/b                 */
#define COP_INT2F     COP_CMD(0x17)  /* in: int;   out: (float)int              */
#define COP_F2INT     COP_CMD(0x18)  /* in: float; out: (int)float (truncate)   */
#define COP_SQRT      COP_CMD(0x1A)  /* in: float; out: float sqrt              */
#define COP_SIN       COP_CMD(0x21)  /* in: i16 angle; out: float sin           */
#define COP_COS       COP_CMD(0x22)  /* in: i16 angle; out: float cos           */
#define COP_M2W       COP_CMD(0x29)  /* in: x,y,z float; out: rot*(x,y,z)+T     */
#define COP_DIST2D    COP_CMD(0x2B)  /* in: x1,z1,x2,z2; out: sqrt((x2-x1)^2+(z2-z1)^2) */
#define COP_ATAN2     COP_CMD(0x2F)  /* in: x1,z1,x2,z2; out: i16 atan2(z2-z1,x2-x1) (azimuth) */
#define COP_WMATRIX   COP_CMD(0x04)  /* in: 12 floats (row-major 3x4) -> bone slot   */
#define COP_RMATRIX   COP_CMD(0x05)  /* out: 12 floats (row-major 3x4) bone -> FIFO  */
#define COP_SET_POS   COP_CMD(0x06)  /* in: x,y,z; T += rot*(x,y,z)                  */
#define COP_SCALE     COP_CMD(0x07)  /* in: sx,sy,sz; rot[col][row] *= s per col     */
#define COP_SUBMIT    COP_CMD(0x78)  /* polygon_submit: commit the transformed object */

/* extended scalar / vector math (cpres1 handler bodies, disasm-verified). The three marked
 * "no STF opcode" are valid cpres1 handlers the shipping game never emits, but which still run. */
#define COP_RSQRT     COP_CMD(0x19)  /* in: float; out: 1/sqrt(x) (0 if x<=0)        */
#define COP_TAN       COP_CMD(0x23)  /* in: i16 angle; out: float tan (sin/cos)      */
#define COP_SINSCALE  COP_CMD(0x24)  /* in: i16 angle, s; out: sin(angle)*s          */
#define COP_COSSCALE  COP_CMD(0x25)  /* in: i16 angle, s; out: cos(angle)*s          */
#define COP_ASIN      COP_CMD(0x26)  /* in: float [-1,1]; out: i16 asin              */
#define COP_ATAN2F    COP_CMD(0x27)  /* in: x,y float; out: i16 atan2(y,x)           */
#define COP_DOT3      COP_CMD(0x2A)  /* in: a0,b0,a1,b1,a2,b2; out: a.b  (no STF op) */
#define COP_DIST3D    COP_CMD(0x2C)  /* in: x1,x2,y1,y2,z1,z2; out: 3D distance      */
#define COP_MAG2D     COP_CMD(0x2D)  /* in: a,b; out: sqrt(a^2+b^2)                  */
#define COP_MAG3D     COP_CMD(0x2E)  /* in: a,b,c; out: sqrt(a^2+b^2+c^2) (no STF op)*/
#define COP_NORM3     COP_CMD(0x30)  /* in: x,y,z; out: unit vector (cpres1; see m2_math.h note) */
#define COP_DOT2D     COP_CMD(0x59)  /* in: a,b,c,d; out: a*b+c*d                    */
#define COP_NORM2     COP_CMD(0x5A)  /* in: x,y; out: unit vector  (cpres1; see m2_math.h note)  */
#define COP_ROT2D     COP_CMD(0x5B)  /* in: i16 angle,x,y; out: rotated x,y (cpres1) */
#define COP_ADD3      COP_CMD(0x5C)  /* in: a0,b0,a1,b1,a2,b2; out: a+b              */
#define COP_SUB3      COP_CMD(0x5D)  /* in: a0,b0,a1,b1,a2,b2; out: a-b  (no STF op) */
#define COP_SCALE3    COP_CMD(0x5E)  /* in: s,x,y,z; out: s*x,s*y,s*z                */
#define COP_W2M       COP_CMD(0x6A)  /* in: x,y,z; out: rot*((x,y,z)-T) world->model */

/* ==== GEO (cpres2) display-list opcodes =======================================================
 * Same M2_SHARC_CMD(n) encoding as the COP (all 12 verified: e.g. LOD n=0x16 ->
 * (0x16<<23)|(0x16<<8)|0x16 = 0x0B001616). STF's encodings, verified vs MAME geo_parse. */
#define GEO_OP(n)         M2_SHARC_CMD(n)
#define GEO_OP_OBJECT     GEO_OP(0x01)  /* object_data: tpa, tha, oba, obc    */
#define GEO_OP_DIRECT     GEO_OP(0x02)  /* direct_data: screen-space geometry */
#define GEO_OP_WINDOW     GEO_OP(0x03)  /* window/clip: 6 coords              */
#define GEO_OP_TEXDATA    GEO_OP(0x04)  /* texture-data -> texture_ram slots  */
#define GEO_OP_TEXPARAM   GEO_OP(0x06)  /* texture params: index, count, ...  */
#define GEO_OP_MODE       GEO_OP(0x07)  /* geo mode (&3 selects vtx parser)   */
#define GEO_OP_ZSORT      GEO_OP(0x08)  /* zsort mode                         */
#define GEO_OP_FOCAL      GEO_OP(0x09)  /* focal distance: fx, fy             */
#define GEO_OP_LIGHT      GEO_OP(0x0A)  /* light vector: x, y, z              */
#define GEO_OP_MATRIX     GEO_OP(0x0B)  /* transform matrix: 12 floats        */
#define GEO_OP_END        GEO_OP(0x0F)  /* end of list                        */
#define GEO_OP_LOD        GEO_OP(0x16)  /* LOD                                */

/* geo mode low 2 bits select the polygon vertex parser. */
#define GEO_MODE_NP_NS  0u   /* normals present, no specular */
#define GEO_MODE_NP_S   1u   /* normals present, specular    */
#define GEO_MODE_NN_NS  2u   /* no normals,      no specular */
#define GEO_MODE_NN_S   3u   /* no normals,      specular    */

#define GEO_TEXRAM_BIT  0x00800000u  /* tpa/tha bit23 = address texture_ram, not the ROMs */
/* direct_data polygon attribute word: quad | linktype 1 | doubleside (== 0x00020101). */
#define GEO_POLY_QUAD   (1u | (1u << 8) | (1u << 17))

/* ==== GEO slot registers ======================================================================
 * Slot numbers observed across STF; arm with m2_geo_cmd(slot) (m2_3d.h), which writes the
 * slot's key word (slot>>4)*0x101 to GEO_START+slot — the payload follows on the GEO FIFO.
 * Slots whose semantics are only partially understood keep the slot number in the name
 * (TABLE40/TABLE50/LUT140) rather than a guessed meaning. */
#define GEO_SLOT_WINDOW    0x030u  /* clip window: 6 packed coords                              */
#define GEO_SLOT_TABLE40   0x040u  /* region/record table stream: base, count, data
                                    * (geo_region_fill; STF sub_12090 / sub_12138)              */
#define GEO_SLOT_TABLE50   0x050u  /* config table stream (STF sub_11EF8)                       */
#define GEO_SLOT_MATERIAL  0x060u  /* material shading coefficients (STF sub_29148)             */
#define GEO_SLOT_MMODE     0x070u  /* per-frame mode (STF set_mmode @0x4C94)                    */
#define GEO_SLOT_ZMODE     0x080u  /* per-frame Z-clip render state (STF set_window_data @0x35E0) */
#define GEO_SLOT_FOCAL     0x090u  /* focal-length pair (STF camera_init :30836)                */
#define GEO_SLOT_LIGHT     0x0A0u  /* light vector: 3 words                                     */
#define GEO_SLOT_ENDMARK   0x0F0u  /* end-mark command (STF set_end_mark @0x354C)               */
#define GEO_SLOT_COMMIT    0x100u  /* table-stream commit: arm + one trailing word closes every
                                    * config upload (observed tail of all STF table loads)      */
#define GEO_SLOT_LUT140    0x140u  /* bit-packed index LUT (STF sub_11E08)                      */
#define GEO_SLOT_PROJSCALE 0x160u  /* z-projection scale = distance * focal (camera_init :30848) */

/* ==== fixed RAM regions (3D colour / texture pipeline) ======================================== */
#define M2_PALRAM     0x01800000u  /* poly palette RAM: colorbase -> palram[cb+0x1000] (BGR555)  */
#define M2_COLORXLAT  0x01810000u  /* R @+0x0000, G @+0x4000, B @+0x8000 (u16)                   */
#define M2_LUMARAM    0x11400000u  /* polygon luma RAM (make_luma_ram fills it from the data ROM)*/
#define TEXRAM_0_1    0x11100000u  /* texram sheet bank (mip ping-pong partner of TEXRAM_1)      */
#define TEXRAM_1      0x11300000u  /* texram sheet bank (STF texram_1)                           */
#define GEO_ZCLIP_REG 0x0181C000u  /* _3D_ZCLIP_START (write 0xFF byte / 0xFFFF00FF word to
                                    * disable board-level near clip)                             */
#define ZCLIP_REG     GEO_ZCLIP_REG   /* legacy alias */

#endif /* M2_CONSTANTS_H */
