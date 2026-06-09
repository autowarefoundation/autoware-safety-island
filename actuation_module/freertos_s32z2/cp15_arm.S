// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// ARM-mode replacements for NXP's RTD Thumb-encoded CP15 / VFP accessors,
// hooked in via `-Wl,--wrap=read_CPACR,...` so callers reach these
// instead of NXP's. NXP ships read_CPACR / write_CPACR / write_FPEXC
// inside core_utility.s with `.thumb2`. Core_FPU_Init in core.c is
// ARM-mode and reaches them via `blx`; the `blx` correctly switches to
// Thumb on entry, but the Thumb `mov pc, lr` epilogue does *not*
// interwork back to ARM on this Cortex-R52 — the T bit stays set, the
// caller's ARM-encoded body is decoded as Thumb-2, and the CPU branches
// off into the .intc_vector data area. Replacing the helpers with
// ARM-mode versions that finish with `bx lr` keeps the LSB-clear LR set
// by the caller's `blx`, so the return is interworked correctly and
// control comes back to the caller in ARM state.

    .syntax unified
    .arch armv8-r
    .arm

    .section .sram_code, "ax"

    .global __wrap_read_CPACR
    .type   __wrap_read_CPACR, %function
__wrap_read_CPACR:
    mrc     p15, 0, r0, c1, c0, 2
    bx      lr
    .size   __wrap_read_CPACR, . - __wrap_read_CPACR

    .global __wrap_write_CPACR
    .type   __wrap_write_CPACR, %function
__wrap_write_CPACR:
    mcr     p15, 0, r0, c1, c0, 2
    isb     sy
    bx      lr
    .size   __wrap_write_CPACR, . - __wrap_write_CPACR

    .global __wrap_write_FPEXC
    .type   __wrap_write_FPEXC, %function
__wrap_write_FPEXC:
    vmsr    fpexc, r0
    isb     sy
    bx      lr
    .size   __wrap_write_FPEXC, . - __wrap_write_FPEXC

    .global __wrap_read_FPEXC
    .type   __wrap_read_FPEXC, %function
__wrap_read_FPEXC:
    vmrs    r0, fpexc
    bx      lr
    .size   __wrap_read_FPEXC, . - __wrap_read_FPEXC

    // write_64bit(lo=r0, hi=r1, dest=r2) — 64-bit atomic store for ECC SRAM.
    // NXP's Thumb-encoded original has the same `mov pc, lr` non-interwork
    // problem as the CPACR helpers; init_data_bss → zero_copy64 → write_64bit
    // is the chain that derailed before this wrap was added.
    .global __wrap_write_64bit
    .type   __wrap_write_64bit, %function
__wrap_write_64bit:
    strd    r0, r1, [r2]
    bx      lr
    .size   __wrap_write_64bit, . - __wrap_write_64bit

    // ===== CP15 / MPU / GIC helpers =====
    // The same `mov pc, lr` interworking failure also affects every other
    // CP15 accessor NXP ships in core_utility.s. SystemInit → Core_IC_Init
    // and the GIC500 init path call these via `blx` from ARM C code, and
    // any non-interworked return derails into Thumb decode of ARM bytes.
    // Wrap every helper that is actually called via BLX in the current
    // build (15 total, identified by `objdump -d | grep 'blx 321012...'`).
    //
    // Encodings are byte-identical to NXP's originals at 0x32101276..
    // 0x32101324, just re-emitted as ARM-mode with a `bx lr` epilogue.

    .global __wrap_read_SCTLR
    .type   __wrap_read_SCTLR, %function
__wrap_read_SCTLR:
    mrc     p15, 0, r0, c1, c0, 0
    bx      lr
    .size   __wrap_read_SCTLR, . - __wrap_read_SCTLR

    .global __wrap_write_SCTLR
    .type   __wrap_write_SCTLR, %function
__wrap_write_SCTLR:
    mcr     p15, 0, r0, c1, c0, 0
    isb     sy
    bx      lr
    .size   __wrap_write_SCTLR, . - __wrap_write_SCTLR

    .global __wrap_write_PRSELR
    .type   __wrap_write_PRSELR, %function
__wrap_write_PRSELR:
    mcr     p15, 0, r0, c6, c2, 1
    isb     sy
    bx      lr
    .size   __wrap_write_PRSELR, . - __wrap_write_PRSELR

    .global __wrap_write_PRBAR
    .type   __wrap_write_PRBAR, %function
__wrap_write_PRBAR:
    mcr     p15, 0, r0, c6, c3, 0
    bx      lr
    .size   __wrap_write_PRBAR, . - __wrap_write_PRBAR

    .global __wrap_write_PRLAR
    .type   __wrap_write_PRLAR, %function
__wrap_write_PRLAR:
    mcr     p15, 0, r0, c6, c3, 1
    bx      lr
    .size   __wrap_write_PRLAR, . - __wrap_write_PRLAR

    .global __wrap_write_MAIR0
    .type   __wrap_write_MAIR0, %function
__wrap_write_MAIR0:
    mcr     p15, 0, r0, c10, c2, 0
    bx      lr
    .size   __wrap_write_MAIR0, . - __wrap_write_MAIR0

    .global __wrap_write_MAIR1
    .type   __wrap_write_MAIR1, %function
__wrap_write_MAIR1:
    mcr     p15, 0, r0, c10, c2, 1
    bx      lr
    .size   __wrap_write_MAIR1, . - __wrap_write_MAIR1

    .global __wrap_read_MPIDR
    .type   __wrap_read_MPIDR, %function
__wrap_read_MPIDR:
    mrc     p15, 0, r0, c0, c0, 5
    bx      lr
    .size   __wrap_read_MPIDR, . - __wrap_read_MPIDR

    .global __wrap_write_ICC_SRE
    .type   __wrap_write_ICC_SRE, %function
__wrap_write_ICC_SRE:
    mcr     p15, 0, r0, c12, c12, 5
    isb     sy
    bx      lr
    .size   __wrap_write_ICC_SRE, . - __wrap_write_ICC_SRE

    // gic500_enableCpuInterface reaches write_ICC_PMR via *plain* ARM `bl`,
    // not `blx` — the compiler/linker did not insert an interworking veneer.
    // Without a wrap the ARM caller jumps to a Thumb-encoded body and decodes
    // its first halfword as ARM (UndefInstr). All other ICC_* helpers in the
    // same call sequence happen to be reached via `blx` and are caught by
    // the BLX-only audit; PMR is the lone exception, so it needs a wrap too.
    .global __wrap_write_ICC_PMR
    .type   __wrap_write_ICC_PMR, %function
__wrap_write_ICC_PMR:
    mcr     p15, 0, r0, c4, c6, 0
    isb     sy
    bx      lr
    .size   __wrap_write_ICC_PMR, . - __wrap_write_ICC_PMR

    .global __wrap_write_ICC_BPR0
    .type   __wrap_write_ICC_BPR0, %function
__wrap_write_ICC_BPR0:
    mcr     p15, 0, r0, c12, c8, 3
    bx      lr
    .size   __wrap_write_ICC_BPR0, . - __wrap_write_ICC_BPR0

    .global __wrap_write_ICC_BPR1
    .type   __wrap_write_ICC_BPR1, %function
__wrap_write_ICC_BPR1:
    mcr     p15, 0, r0, c12, c12, 3
    bx      lr
    .size   __wrap_write_ICC_BPR1, . - __wrap_write_ICC_BPR1

    .global __wrap_write_ICC_IGRPEN0
    .type   __wrap_write_ICC_IGRPEN0, %function
__wrap_write_ICC_IGRPEN0:
    mcr     p15, 0, r0, c12, c12, 6
    isb     sy
    bx      lr
    .size   __wrap_write_ICC_IGRPEN0, . - __wrap_write_ICC_IGRPEN0

    .global __wrap_write_ICC_IGRPEN1
    .type   __wrap_write_ICC_IGRPEN1, %function
__wrap_write_ICC_IGRPEN1:
    mcr     p15, 0, r0, c12, c12, 7
    isb     sy
    bx      lr
    .size   __wrap_write_ICC_IGRPEN1, . - __wrap_write_ICC_IGRPEN1

    .global __wrap_read_ICC_CTLR
    .type   __wrap_read_ICC_CTLR, %function
__wrap_read_ICC_CTLR:
    mrc     p15, 0, r0, c12, c12, 4
    bx      lr
    .size   __wrap_read_ICC_CTLR, . - __wrap_read_ICC_CTLR

    .global __wrap_write_ICC_CTLR
    .type   __wrap_write_ICC_CTLR, %function
__wrap_write_ICC_CTLR:
    mcr     p15, 0, r0, c12, c12, 4
    isb     sy
    bx      lr
    .size   __wrap_write_ICC_CTLR, . - __wrap_write_ICC_CTLR

    // ===== Undefined-Instruction vector -> FreeRTOS lazy-FPU handler =====
    // NXP's Vector_Table.s routes the undef vector to a .weak UndefInstr_Handler
    // (exceptions.c) that just spins. The ARM_CR52_GIC port instead ships its
    // own vPortUndefinedInstruction (port.c, built when configENABLE_FPU==1)
    // that lazily re-enables the FPU on a task's first VFP instruction and
    // manages per-task FP context -- but it is never wired up. The port clears
    // FPEXC.EN on every task switch and *relies* on this handler to re-enable
    // it, so without the wiring the first VFP op in any task (e.g. _vfprintf_r's
    // `vpush {d8-d9}`) traps into the spinning default and hangs.
    //
    // Provide a strong UndefInstr_Handler (overriding the weak one) that jumps
    // to vPortUndefinedInstruction. The vector reaches this exactly as it
    // reached the spinning default (ARM-mode, UND mode, LR_und set); the
    // `ldr pc, =` interworks to the port handler (Thumb) without disturbing LR,
    // which vPortUndefinedInstruction adjusts itself. FP-using tasks must be
    // created with xTaskCreateFpu so the handler finds their FP context in TLS.
    .global UndefInstr_Handler
    .type   UndefInstr_Handler, %function
UndefInstr_Handler:
    ldr     pc, 1f
1:  .word   vPortUndefinedInstruction
    .size   UndefInstr_Handler, . - UndefInstr_Handler

    // ===== IRQ vector -> FreeRTOS interrupt dispatcher =====
    // NXP's Vector_Table.s routes the IRQ vector to a weak IRQ_Handler
    // (exceptions_stack.s) that saves the interrupted context onto the SYS
    // (task) stack, calls the registered ISR, and returns. It cannot perform a
    // FreeRTOS context switch: xPortSysTickHandler calls vTaskSwitchContext()
    // (updating pxCurrentTCB) but the generic handler never swaps stacks, and
    // the periodic-timer IRQ data-aborts in its `ldmia sp!, {r0-r12}` epilogue.
    //
    // The port ships vPortInterruptDispatcher (port.c) -- the real FreeRTOS IRQ
    // entry that reads ICC_IAR1, dispatches low-priority ISRs, and routes OS
    // interrupts (>= configMAX_SYSCALL_INTERRUPT_PRIORITY) through its osInt
    // path which does the full save / vTaskSwitchContext / restore. Override
    // the weak IRQ_Handler to jump to it. It uses the IRQ/SVC mode stacks that
    // startup.s set up, and looks up ISRs via __VECTOR_RAM (defsym'd to the
    // core-0 RAM vector table that NXP's startup populates).
    .global IRQ_Handler
    .type   IRQ_Handler, %function
IRQ_Handler:
    ldr     pc, 2f
2:  .word   vPortInterruptDispatcher
    .size   IRQ_Handler, . - IRQ_Handler
