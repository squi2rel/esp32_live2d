.section .text

.align 4
.global pgl_lane_add4_f32
.type pgl_lane_add4_f32, @function
pgl_lane_add4_f32:
    entry a1, 16
    ee.ldf.128.ip f3, f2, f1, f0, a2, 16
    ee.ldf.128.ip f7, f6, f5, f4, a3, 16
    add.s f3, f3, f7
    add.s f2, f2, f6
    add.s f1, f1, f5
    add.s f0, f0, f4
    ee.stf.128.ip f3, f2, f1, f0, a4, 16
    movi.n a2, 0
    retw.n

.align 4
.global pgl_lane_sub4_f32
.type pgl_lane_sub4_f32, @function
pgl_lane_sub4_f32:
    entry a1, 16
    ee.ldf.128.ip f3, f2, f1, f0, a2, 16
    ee.ldf.128.ip f7, f6, f5, f4, a3, 16
    sub.s f3, f3, f7
    sub.s f2, f2, f6
    sub.s f1, f1, f5
    sub.s f0, f0, f4
    ee.stf.128.ip f3, f2, f1, f0, a4, 16
    movi.n a2, 0
    retw.n

.align 4
.global pgl_lane_mul4_f32
.type pgl_lane_mul4_f32, @function
pgl_lane_mul4_f32:
    entry a1, 16
    ee.ldf.128.ip f3, f2, f1, f0, a2, 16
    ee.ldf.128.ip f7, f6, f5, f4, a3, 16
    mul.s f3, f3, f7
    mul.s f2, f2, f6
    mul.s f1, f1, f5
    mul.s f0, f0, f4
    ee.stf.128.ip f3, f2, f1, f0, a4, 16
    movi.n a2, 0
    retw.n

.align 4
.global pgl_lane_mul_scalar4_f32
.type pgl_lane_mul_scalar4_f32, @function
pgl_lane_mul_scalar4_f32:
    entry a1, 16
    lsip f4, a3, 0
    ee.ldf.128.ip f3, f2, f1, f0, a2, 16
    mul.s f3, f3, f4
    mul.s f2, f2, f4
    mul.s f1, f1, f4
    mul.s f0, f0, f4
    ee.stf.128.ip f3, f2, f1, f0, a4, 16
    movi.n a2, 0
    retw.n

.align 4
.global pgl_lane_lerp4_f32
.type pgl_lane_lerp4_f32, @function
pgl_lane_lerp4_f32:
    entry a1, 16
    ee.ldf.128.ip f3, f2, f1, f0, a2, 16
    ee.ldf.128.ip f7, f6, f5, f4, a3, 16
    ee.ldf.128.ip f11, f10, f9, f8, a4, 16
    ee.ldf.128.ip f15, f14, f13, f12, a5, 16
    mul.s f3, f3, f11
    mul.s f2, f2, f10
    mul.s f1, f1, f9
    mul.s f0, f0, f8
    mul.s f7, f7, f15
    mul.s f6, f6, f14
    mul.s f5, f5, f13
    mul.s f4, f4, f12
    add.s f3, f3, f7
    add.s f2, f2, f6
    add.s f1, f1, f5
    add.s f0, f0, f4
    ee.stf.128.ip f3, f2, f1, f0, a6, 16
    movi.n a2, 0
    retw.n

.align 4
.global pgl_qr_mix_u16x8
.type pgl_qr_mix_u16x8, @function
pgl_qr_mix_u16x8:
    entry a1, 16
    ee.vld.128.ip q0, a2, 16
    ee.vld.128.ip q1, a3, 16
    ee.vld.128.ip q2, a4, 16
    ee.vld.128.ip q3, a5, 16
    ee.zero.qacc
    ee.vmulas.u16.qacc q0, q2
    ee.vmulas.u16.qacc q1, q3
    ee.srcmb.s16.qacc q4, a6, 0
    ee.vst.128.ip q4, a7, 16
    movi.n a2, 0
    retw.n

.align 4
.global pgl_qacc_bilinear_u16x8
.type pgl_qacc_bilinear_u16x8, @function
pgl_qacc_bilinear_u16x8:
    entry a1, 16
    ee.vld.128.ip q0, a2, 16
    ee.vld.128.ip q1, a2, 16
    ee.vld.128.ip q2, a2, 16
    ee.vld.128.ip q3, a2, 16
    ee.vld.128.ip q4, a3, 16
    ee.vld.128.ip q5, a3, 16
    ee.vld.128.ip q6, a3, 16
    ee.vld.128.ip q7, a4, 16

    ee.mov.u16.qacc q7
    ee.vmulas.u16.qacc q0, q4
    ee.vmulas.u16.qacc q1, q5
    ee.srcmb.s16.qacc q0, a5, 0

    ee.mov.u16.qacc q7
    ee.vmulas.u16.qacc q2, q4
    ee.vmulas.u16.qacc q3, q5
    ee.srcmb.s16.qacc q1, a5, 0

    ee.vld.128.ip q3, a3, 16
    ee.mov.u16.qacc q7
    ee.vmulas.u16.qacc q0, q6
    ee.vmulas.u16.qacc q1, q3
    ee.srcmb.s16.qacc q2, a5, 0
    ee.vst.128.ip q2, a6, 16
    movi.n a2, 0
    retw.n
