/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd. All rights reserved.
 * libobmm-accel is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 *
 * Description: NC copy kernel (ST64B batches)
 * Invariants:
 *   - every load within [src, src+n), every store within [dst, dst+n)
 *   - LS64 instructions only in kernels named *_st64b / *_ld64b
 *   - every symbol here must carry .hidden: assembly is not covered
 *     by the C side's -fvisibility=hidden, and without it the symbol
 *     leaks into the export table (no version script to clamp it)
 *
 * Register contract: x0=dst x1=src x2=n (AAPCS)
 * Author: Yuhao Zhang
 * Create: 2026-11-06
 */

.arch armv8.4-a+ls64

.text
.balign 4

/*
 * void nc_copy_st64b(void *dst [NC], const void *src, size_t n)
 * Any alignment; self-aligning head/tail byte ladders.
 * head: byte-copy until dst is 64B aligned (<=63B, in-bounds)
 * body: 3-window batch (K=3): 24x ldr x (any alignment legal on CC src)
 *       then 3x ST64B back-to-back
 * tail: byte ladder (<64B)
 * Windows: x4-x11, x12-x19, x20-x27 (bases even, <=x22). Saves x19-x27.
 */
.global nc_copy_st64b
.hidden nc_copy_st64b
.type nc_copy_st64b, %function
nc_copy_st64b:
        .cfi_startproc
        stp     x29, x30, [sp, #-96]!
        stp     x19, x20, [sp, #16]
        stp     x21, x22, [sp, #32]
        stp     x23, x24, [sp, #48]
        stp     x25, x26, [sp, #64]
        str     x27, [sp, #80]
        .cfi_def_cfa_offset 96
        .cfi_offset x29, -96
        .cfi_offset x30, -88
        .cfi_offset x19, -80
        .cfi_offset x20, -72
        .cfi_offset x21, -64
        .cfi_offset x22, -56
        .cfi_offset x23, -48
        .cfi_offset x24, -40
        .cfi_offset x25, -32
        .cfi_offset x26, -24
        .cfi_offset x27, -16
        cbz     x2, .Lst64b_done
        /* head: dst -> 64B aligned */
        ands    x3, x0, #63
        b.eq    .Lst64b_body3
.Lst64b_head:
        ldrb    w4, [x1], #1
        strb    w4, [x0], #1
        subs    x2, x2, #1
        b.eq    .Lst64b_done
        ands    x3, x0, #63
        b.ne    .Lst64b_head
        /* body: 192B batches */
.Lst64b_body3:
        cmp     x2, #192
        b.lo    .Lst64b_body1
        ldr     x4,  [x1];      ldr x5,  [x1, #8]
        ldr     x6,  [x1, #16]; ldr x7,  [x1, #24]
        ldr     x8,  [x1, #32]; ldr x9,  [x1, #40]
        ldr     x10, [x1, #48]; ldr x11, [x1, #56]
        ldr     x12, [x1, #64]; ldr x13, [x1, #72]
        ldr     x14, [x1, #80]; ldr x15, [x1, #88]
        ldr     x16, [x1, #96]; ldr x17, [x1, #104]
        ldr     x18, [x1, #112];ldr x19, [x1, #120]
        ldr     x20, [x1, #128];ldr x21, [x1, #136]
        ldr     x22, [x1, #144];ldr x23, [x1, #152]
        ldr     x24, [x1, #160];ldr x25, [x1, #168]
        ldr     x26, [x1, #176];ldr x27, [x1, #184]
        /* x18 is part of the x12-based ST64B window: ST64B stores 8
         * consecutive registers, so x12..x19 is a structural requirement,
         * not scratch choice. Not safe under shadow-call-stack ABIs. */
        /* ST64B takes no immediate offset (#0 only): walk x0 between the
         * three window stores (x0 -> x0+64 -> x0+128, +192 total). */
        st64b   x4,  [x0]
        add     x0, x0, #64
        st64b   x12, [x0]
        add     x0, x0, #64
        st64b   x20, [x0]
        add     x0, x0, #64
        add     x1, x1, #192
        subs    x2, x2, #192
        b.hs    .Lst64b_body3
.Lst64b_body1:
        cmp     x2, #64
        b.lo    .Lst64b_tail
        ldr     x4,  [x1];      ldr x5,  [x1, #8]
        ldr     x6,  [x1, #16]; ldr x7,  [x1, #24]
        ldr     x8,  [x1, #32]; ldr x9,  [x1, #40]
        ldr     x10, [x1, #48]; ldr x11, [x1, #56]
        st64b   x4, [x0]
        add     x1, x1, #64
        add     x0, x0, #64
        subs    x2, x2, #64
        b.hs    .Lst64b_body1
.Lst64b_tail:
        cbz     x2, .Lst64b_done
        ldrb    w4, [x1], #1
        strb    w4, [x0], #1
        subs    x2, x2, #1
        b.ne    .Lst64b_tail
.Lst64b_done:
        ldp     x19, x20, [sp, #16]
        ldp     x21, x22, [sp, #32]
        ldp     x23, x24, [sp, #48]
        ldp     x25, x26, [sp, #64]
        ldr     x27, [sp, #80]
        ldp     x29, x30, [sp], #96
        ret
.cfi_endproc
.size nc_copy_st64b, .-nc_copy_st64b

.section .note.GNU-stack,"",%progbits
