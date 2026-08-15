/*
 * Copyright (c) 2026, Arm Limited and contributors.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Task 18 / R1: a replacement ARMv8-R AArch32 (Cortex-R52) exception vector
 * table whose fault vectors print instead of spinning silently.
 *
 * Why a second table instead of a patch to the vendor's:
 * rcar_bsp/FreeRTOS/Demo/R-Car_Gen5_CR52/common/asm_vectors.S is frozen --
 * byte-identical to the vendor sample, and the plan's equivalence argument
 * depends on it staying that way. Its Undefined Instruction, Prefetch
 * Abort, Data Abort, Reserved and FIQ vectors are each an unconditional
 * `b <self>`: no print, no dump, and entering those modes architecturally
 * masks IRQ, which is exactly the symptom the board shows (console stops,
 * and Linux's mfis-mbox interrupt count stops advancing). So this file
 * builds a complete second table and x5h_diag_install_vectors()
 * (x5h_diag.c) points VBAR at it from our own code during early startup.
 * Nothing under rcar_bsp/ changes.
 *
 * The two vectors that must NOT change behaviour are SVC (FreeRTOS context
 * switch) and IRQ (the tick, and every driver interrupt). Those two, and
 * only those two, are replicated instruction-for-instruction: SVC from
 * asm_vectors.S:32-35 (the vendor's BPIALL + DSB prologue, then the branch
 * to SVC_Handler) and IRQ from asm_vectors.S:46-47. The vendor's five fault
 * handlers -- undefined_handler (29-30), prefetch_handler (37-38),
 * abort_handler (40-41), reserved_handler (43-44), fiq_handler (49-50) --
 * are emphatically NOT replicated; replacing each bare `b <self>` with
 * something that prints is this file's entire reason to exist. Both
 * replicated handlers are
 * ARM-state naked functions in rcar_bsp/.../common/ARM_CR52/port.c
 * (BSP_TARGET_ARM + __attribute__((naked)), lines 705 and 732), and neither
 * the vendor's `b` nor this file's touches LR or any GPR, so the state each
 * handler sees on entry is identical either way. The reset slot keeps the
 * vendor's `ldr pc, =_reset` for completeness; on this SoC reset is fetched
 * from the reset base address, not through VBAR.
 *
 * Entry-stub contract with x5h_diag_exception_report(uint32_t kind,
 * uint32_t pc, uint32_t spsr): r0 = one of the X5H_DIAG_EXC_* codes below,
 * r1 = the preferred faulting address (LR corrected by the architectural
 * per-exception return offset), r2 = SPSR. The stubs deliberately do no
 * more than that: DFSR/DFAR/IFSR/IFAR are only written by a FAULTING access
 * and so survive everything the stub and the C prologue do (see
 * x5h_diag_exception_report()'s own comment), and the report function never
 * returns, so nothing needs saving. Each exception mode has its own stack
 * installed by boot.S (common/linker/lscript_vram2.ld's _ABORT_STACK_SIZE /
 * _UNDEF_STACK_SIZE / _FIQ_STACK_SIZE), which is what the C side runs on.
 *
 * `ldr pc, =sym` rather than `b sym` for the C entry point: LDR to PC is an
 * interworking branch on ARMv5 and later, so it is correct whether GCC
 * emitted x5h_diag_exception_report in ARM or Thumb state, and it has no
 * ±32 MiB displacement limit. The intra-file trampolines below use `b`
 * because they mirror the vendor file's own `b`, and both targets are
 * ARM-state.
 */

#include "x5h_diag.h"

    .syntax unified
    .arm

    .extern _reset
    .extern SVC_Handler
    .extern IRQ_Handler
    .extern x5h_diag_exception_report

/*
 * The table itself. Deliberately in an ordinary .text.* input section --
 * collected by lscript_vram2.ld's `*(.text.*)` -- and NOT in .isr_vector or
 * .vectors: those two are gathered at the very start of .text, immediately
 * after the linker script's `_vector_table = .`, so putting anything of
 * ours there would shift the vendor table and move the frozen
 * .text @ 0x11600000 entry point.
 *
 * VBAR ignores bits [4:0], so the table must be 32-byte aligned. .balign 32
 * raises this input section's own alignment to 32, which the linker
 * preserves when it places the section; x5h_diag_install_vectors() also
 * re-checks the final linked address at run time before programming VBAR.
 *
 * .ltorg immediately after the eight entries puts the literal pool at
 * table+32, well inside `ldr pc, [pc, #imm12]` reach from every entry.
 */
    .section .text.x5h_diag_vector_table, "ax", %progbits
    .balign 32
    .global x5h_diag_vector_table
    .type x5h_diag_vector_table, %object
x5h_diag_vector_table:
    ldr pc, =_reset                     /* 0x00 Reset                    */
    ldr pc, =x5h_diag_undef_entry       /* 0x04 Undefined Instruction    */
    ldr pc, =x5h_diag_svc_trampoline    /* 0x08 Supervisor Call          */
    ldr pc, =x5h_diag_pabt_entry        /* 0x0C Prefetch Abort           */
    ldr pc, =x5h_diag_dabt_entry        /* 0x10 Data Abort               */
    ldr pc, =x5h_diag_resv_entry        /* 0x14 Reserved                 */
    ldr pc, =x5h_diag_irq_trampoline    /* 0x18 IRQ                      */
    ldr pc, =x5h_diag_fiq_entry         /* 0x1C FIQ                      */
    .size x5h_diag_vector_table, . - x5h_diag_vector_table
    .ltorg

    .section .text.x5h_diag_handlers, "ax", %progbits
    .balign 4

/*
 * Unchanged behaviour, replicated from asm_vectors.S:32-35. BPIALL
 * (MCR p15, 0, Rd, c7, c5, 6) ignores the value written, so reading r0 here
 * is not a use of the caller's r0 -- it is the vendor's own encoding, kept
 * verbatim.
 */
    .global x5h_diag_svc_trampoline
    .type x5h_diag_svc_trampoline, %function
x5h_diag_svc_trampoline:
    mcr p15, 0, r0, c7, c5, 6           /* BPIALL */
    dsb
    b   SVC_Handler
    .size x5h_diag_svc_trampoline, . - x5h_diag_svc_trampoline

/* Unchanged behaviour, replicated from asm_vectors.S:46-47. */
    .global x5h_diag_irq_trampoline
    .type x5h_diag_irq_trampoline, %function
x5h_diag_irq_trampoline:
    b   IRQ_Handler
    .size x5h_diag_irq_trampoline, . - x5h_diag_irq_trampoline

/*
 * Undefined Instruction: LR_und = address of the undefined instruction + 4
 * in ARM state, so the preferred faulting address is LR - 4.
 */
    .global x5h_diag_undef_entry
    .type x5h_diag_undef_entry, %function
x5h_diag_undef_entry:
    sub r1, lr, #4
    mrs r2, spsr
    mov r0, #X5H_DIAG_EXC_UNDEF
    ldr pc, =x5h_diag_exception_report
    .size x5h_diag_undef_entry, . - x5h_diag_undef_entry

/* Prefetch Abort: LR_abt = aborted instruction address + 4. */
    .global x5h_diag_pabt_entry
    .type x5h_diag_pabt_entry, %function
x5h_diag_pabt_entry:
    sub r1, lr, #4
    mrs r2, spsr
    mov r0, #X5H_DIAG_EXC_PABT
    ldr pc, =x5h_diag_exception_report
    .size x5h_diag_pabt_entry, . - x5h_diag_pabt_entry

/* Data Abort: LR_abt = aborting instruction address + 8. */
    .global x5h_diag_dabt_entry
    .type x5h_diag_dabt_entry, %function
x5h_diag_dabt_entry:
    sub r1, lr, #8
    mrs r2, spsr
    mov r0, #X5H_DIAG_EXC_DABT
    ldr pc, =x5h_diag_exception_report
    .size x5h_diag_dabt_entry, . - x5h_diag_dabt_entry

/*
 * Reserved vector: the architecture defines no return-address offset for
 * it, so LR is reported raw rather than "corrected" by a number that would
 * mean nothing. x5h_diag_exception_report() labels this one accordingly.
 */
    .global x5h_diag_resv_entry
    .type x5h_diag_resv_entry, %function
x5h_diag_resv_entry:
    mov r1, lr
    mrs r2, spsr
    mov r0, #X5H_DIAG_EXC_RESV
    ldr pc, =x5h_diag_exception_report
    .size x5h_diag_resv_entry, . - x5h_diag_resv_entry

/* FIQ: LR_fiq = address of the next instruction to execute + 4. */
    .global x5h_diag_fiq_entry
    .type x5h_diag_fiq_entry, %function
x5h_diag_fiq_entry:
    sub r1, lr, #4
    mrs r2, spsr
    mov r0, #X5H_DIAG_EXC_FIQ
    ldr pc, =x5h_diag_exception_report
    .size x5h_diag_fiq_entry, . - x5h_diag_fiq_entry

    .ltorg
