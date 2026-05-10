.section .text
.global display_rgba8888_to_rgb565_be_s3
.type display_rgba8888_to_rgb565_be_s3, @function

/*
 * a2 - dst uint16_t*
 * a3 - src uint32_t* (must be 16-byte aligned)
 * a4 - pixel count (must be a multiple of 4)
 * a5 - pointer to 4 uint32_t masks:
 *      [0] 0x000000F8
 *      [1] 0x0000E000
 *      [2] 0x00001C00
 *      [3] 0x00F80000
 */
display_rgba8888_to_rgb565_be_s3:
    entry a1, 16

    beqz a4, .Ldone
    srli a4, a4, 2
    beqz a4, .Ldone

    ee.vldbc.32.ip q4, a5, 4
    ee.vldbc.32.ip q5, a5, 4
    ee.vldbc.32.ip q6, a5, 4
    ee.vldbc.32.ip q7, a5, 0

    movi.n a10, 13
    movi.n a11, 3
    movi.n a12, 11

    loopnez a4, .Lsimd_loop_end
        ee.vld.128.ip q0, a3, 16

        ee.andq q1, q0, q4
        ee.andq q2, q0, q5
        ee.andq q3, q0, q6
        ee.andq q0, q0, q7

        wsr a10, sar
        ee.vsr.32 q2, q2

        wsr a11, sar
        ee.vsl.32 q3, q3

        wsr a12, sar
        ee.vsr.32 q0, q0

        ee.orq q1, q1, q2
        ee.orq q0, q0, q3
        ee.orq q1, q1, q0

        ee.movi.32.a q1, a6, 0
        ee.movi.32.a q1, a7, 1
        ee.movi.32.a q1, a8, 2
        ee.movi.32.a q1, a9, 3

        s16i a6, a2, 0
        s16i a7, a2, 2
        s16i a8, a2, 4
        s16i a9, a2, 6
        addi.n a2, a2, 8
    .Lsimd_loop_end:

.Ldone:
    retw.n
