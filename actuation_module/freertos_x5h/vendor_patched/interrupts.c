/*
 * Copyright (c) 2019-2020 Renesas Electronics Europe Ltd. All rights reserved.
 *
 * SPDX-License-Identifier: MIT
 */
/* Task 33: patched copy of
 * rcar_bsp/FreeRTOS/Demo/R-Car_Gen5_CR52/common/interrupts.c at submodule
 * pin rcar-v2.5.0 (f129675e3). It shadows that file's object inside
 * libfreertos_bsp.a -- see the X5H_BSP_PATCHED_SOURCES comment in
 * ../CMakeLists.txt for the mechanism and for why the submodule itself is
 * not edited. The only deviation from the vendor file is the redistributor
 * indexing in Irq_SetPriority() and Irq_Disable(); the comment on
 * Irq_SetPriority() below explains the defect. Keep everything else
 * byte-identical to the vendor file so a diff against the submodule shows
 * exactly the fix. */
#include "FreeRTOS.h"
#include "task.h"
#include <stddef.h>
#include "cmsis_rcar_gen5.h"
#include "interrupts.h"
#include "core_cr52.h"
#include "CMSIS_5/irq_ctrl.h"
#include "irq_ctrl.h"
#include "drivers/gic/gic.h"
#include <stdio.h>
#include "cmsis_cp15.h"

#define MAX_IRQ_NUMBER       1019
#define DEFAULT_ISR_PRIORITY IPRIORITY(1)

typedef void (*IrqHandlerFn)(void *data);

typedef struct
{
	IrqHandlerFn Handler;
	Context_t *Context;
} IvtEntry;



static IvtEntry HandlerTable[MAX_IRQ_NUMBER];

#define MASK_DMA_RT(x,y)        (0x18A00000U + 0x93C8U + ((x) * 0x20U) + ((y) * 0x4U))
#define STAT_DMA_RT(x,y)        (0x18A00000U + 0x83C8U + ((x) * 0x20U) + ((y) * 0x4U))
#define MASK_SYS_DMA(x,y)       (0x18A00000U + 0x9468U + ((x) * 0x20U) + ((y) * 0x4U))
#define STAT_SYS_DMA(x,y)       (0x18A00000U + 0x8468U + ((x) * 0x20U) + ((y) * 0x4U))
#define STAT_UCIE(x)            (0x18A00000U + 0x8770U + ((x) * 0x4U))
#define STAT_UCIE_ERROR(x)      (0x18A00000U + 0x8778U + ((x) * 0x4U))
#define MASK_UCIE(x)            (0x18A00000U + 0x9770U + ((x) * 0x4U))
#define MASK_UCIE_ERROR(x)      (0x18A00000U + 0x9778U + ((x) * 0x4U))
#define STAT_VIN(x)             (0x18A00000U + 0x854CU + ((x) * 0x4U))
#define MASK_VIN(x)             (0x18A00000U + 0x954CU + ((x) * 0x4U))
#define STAT_UMF(x)             (0x18A00000U + 0x874CU + ((x) * 0x4U))
#define MASK_UMF(x)             (0x18A00000U + 0x974CU + ((x) * 0x4U))

typedef struct
{
	uint32_t irq;
	uint32_t mask_val;
	uint32_t status_reg;
	uint32_t mask_reg;
} irq_table;

static const irq_table r8a78000_irq_table[] = {
	{ 0x112, 0x3, STAT_DMA_RT(0,0), MASK_DMA_RT(0,0) },
        { 0x113, 0x3, STAT_DMA_RT(0,1), MASK_DMA_RT(0,1) },
        { 0x114, 0x3, STAT_DMA_RT(0,2), MASK_DMA_RT(0,2) },
        { 0x115, 0x3, STAT_DMA_RT(0,3), MASK_DMA_RT(0,3) },
        { 0x116, 0x3, STAT_DMA_RT(0,4), MASK_DMA_RT(0,4) },
        { 0x117, 0x3, STAT_DMA_RT(0,5), MASK_DMA_RT(0,5) },
        { 0x118, 0x3, STAT_DMA_RT(0,6), MASK_DMA_RT(0,6) },
        { 0x119, 0x3, STAT_DMA_RT(0,7), MASK_DMA_RT(0,7) },
        { 0x11A, 0x3, STAT_DMA_RT(1,0), MASK_DMA_RT(1,0) },
        { 0x11B, 0x3, STAT_DMA_RT(1,1), MASK_DMA_RT(1,1) },
        { 0x11C, 0x3, STAT_DMA_RT(1,2), MASK_DMA_RT(1,2) },
        { 0x11D, 0x3, STAT_DMA_RT(1,3), MASK_DMA_RT(1,3) },
        { 0x11E, 0x3, STAT_DMA_RT(1,4), MASK_DMA_RT(1,4) },
        { 0x11F, 0x3, STAT_DMA_RT(1,5), MASK_DMA_RT(1,5) },
        { 0x120, 0x3, STAT_DMA_RT(1,6), MASK_DMA_RT(1,6) },
        { 0x121, 0x3, STAT_DMA_RT(1,7), MASK_DMA_RT(1,7) },
        { 0x122, 0x3, STAT_DMA_RT(2,0), MASK_DMA_RT(2,0) },
        { 0x123, 0x3, STAT_DMA_RT(2,1), MASK_DMA_RT(2,1) },
        { 0x124, 0x3, STAT_DMA_RT(2,2), MASK_DMA_RT(2,2) },
        { 0x125, 0x3, STAT_DMA_RT(2,3), MASK_DMA_RT(2,3) },
        { 0x126, 0x3, STAT_DMA_RT(2,4), MASK_DMA_RT(2,4) },
        { 0x127, 0x3, STAT_DMA_RT(2,5), MASK_DMA_RT(2,5) },
        { 0x128, 0x3, STAT_DMA_RT(2,6), MASK_DMA_RT(2,6) },
        { 0x129, 0x3, STAT_DMA_RT(2,7), MASK_DMA_RT(2,7) },
        { 0x12A, 0x3, STAT_DMA_RT(3,0), MASK_DMA_RT(3,0) },
        { 0x12B, 0x3, STAT_DMA_RT(3,1), MASK_DMA_RT(3,1) },
        { 0x12C, 0x3, STAT_DMA_RT(3,2), MASK_DMA_RT(3,2) },
        { 0x12D, 0x3, STAT_DMA_RT(3,3), MASK_DMA_RT(3,3) },
        { 0x12E, 0x3, STAT_DMA_RT(3,4), MASK_DMA_RT(3,4) },
        { 0x12F, 0x3, STAT_DMA_RT(3,5), MASK_DMA_RT(3,5) },
        { 0x130, 0x3, STAT_DMA_RT(3,6), MASK_DMA_RT(3,6) },
        { 0x131, 0x3, STAT_DMA_RT(3,7), MASK_DMA_RT(3,7) },

        { 0x13A, 0x3, STAT_SYS_DMA(0,0), MASK_SYS_DMA(0,0) },
        { 0x13B, 0x3, STAT_SYS_DMA(0,1), MASK_SYS_DMA(0,1) },
        { 0x13C, 0x3, STAT_SYS_DMA(0,2), MASK_SYS_DMA(0,2) },
        { 0x13D, 0x3, STAT_SYS_DMA(0,3), MASK_SYS_DMA(0,3) },
        { 0x13E, 0x3, STAT_SYS_DMA(0,4), MASK_SYS_DMA(0,4) },
        { 0x13F, 0x3, STAT_SYS_DMA(0,5), MASK_SYS_DMA(0,5) },
        { 0x140, 0x3, STAT_SYS_DMA(0,6), MASK_SYS_DMA(0,6) },
        { 0x141, 0x3, STAT_SYS_DMA(0,7), MASK_SYS_DMA(0,7) },
        { 0x142, 0x3, STAT_SYS_DMA(1,0), MASK_SYS_DMA(1,0) },
        { 0x143, 0x3, STAT_SYS_DMA(1,1), MASK_SYS_DMA(1,1) },
        { 0x144, 0x3, STAT_SYS_DMA(1,2), MASK_SYS_DMA(1,2) },
        { 0x145, 0x3, STAT_SYS_DMA(1,3), MASK_SYS_DMA(1,3) },
        { 0x146, 0x3, STAT_SYS_DMA(1,4), MASK_SYS_DMA(1,4) },
        { 0x147, 0x3, STAT_SYS_DMA(1,5), MASK_SYS_DMA(1,5) },
        { 0x148, 0x3, STAT_SYS_DMA(1,6), MASK_SYS_DMA(1,6) },
        { 0x149, 0x3, STAT_SYS_DMA(1,7), MASK_SYS_DMA(1,7) },
        { 0x14A, 0x3, STAT_SYS_DMA(2,0), MASK_SYS_DMA(2,0) },
        { 0x14B, 0x3, STAT_SYS_DMA(2,1), MASK_SYS_DMA(2,1) },
        { 0x14C, 0x3, STAT_SYS_DMA(2,2), MASK_SYS_DMA(2,2) },
        { 0x14D, 0x3, STAT_SYS_DMA(2,3), MASK_SYS_DMA(2,3) },
        { 0x14E, 0x3, STAT_SYS_DMA(2,4), MASK_SYS_DMA(2,4) },
        { 0x14F, 0x3, STAT_SYS_DMA(2,5), MASK_SYS_DMA(2,5) },
        { 0x150, 0x3, STAT_SYS_DMA(2,6), MASK_SYS_DMA(2,6) },
        { 0x151, 0x3, STAT_SYS_DMA(2,7), MASK_SYS_DMA(2,7) },
        { 0x152, 0x3, STAT_SYS_DMA(3,0), MASK_SYS_DMA(3,0) },
        { 0x153, 0x3, STAT_SYS_DMA(3,1), MASK_SYS_DMA(3,1) },
        { 0x154, 0x3, STAT_SYS_DMA(3,2), MASK_SYS_DMA(3,2) },
        { 0x155, 0x3, STAT_SYS_DMA(3,3), MASK_SYS_DMA(3,3) },
        { 0x156, 0x3, STAT_SYS_DMA(3,4), MASK_SYS_DMA(3,4) },
        { 0x157, 0x3, STAT_SYS_DMA(3,5), MASK_SYS_DMA(3,5) },
        { 0x158, 0x3, STAT_SYS_DMA(3,6), MASK_SYS_DMA(3,6) },
        { 0x159, 0x3, STAT_SYS_DMA(3,7), MASK_SYS_DMA(3,7) },

        { 0x173, 0xFF, STAT_VIN(0), MASK_VIN(0) },
        { 0x174, 0xFF, STAT_VIN(1), MASK_VIN(1) },
        { 0x175, 0xFF, STAT_VIN(2), MASK_VIN(2) },
        { 0x176, 0xFF, STAT_VIN(3), MASK_VIN(3) },
        { 0x177, 0xFF, STAT_VIN(4), MASK_VIN(4) },
        { 0x178, 0xFF, STAT_VIN(5), MASK_VIN(5) },
        { 0x179, 0xFF, STAT_VIN(6), MASK_VIN(6) },
        { 0x17A, 0xFF, STAT_VIN(7), MASK_VIN(7) },
        { 0x17B, 0xFF, STAT_VIN(8), MASK_VIN(8) },
        { 0x17C, 0xFF, STAT_VIN(9), MASK_VIN(9) },
        { 0x17D, 0xFF, STAT_VIN(10), MASK_VIN(10) },
        { 0x17E, 0xFF, STAT_VIN(11), MASK_VIN(11) },

        { 0x1FC, 0x1F, STAT_UCIE(0), MASK_UCIE(0) },
        { 0x1FD, 0x1F, STAT_UCIE(1), MASK_UCIE(1) },
        { 0x1FE, 0x1FF, STAT_UCIE_ERROR(0), MASK_UCIE_ERROR(0) },
        { 0x1FF, 0x1FF, STAT_UCIE_ERROR(1), MASK_UCIE_ERROR(1) },

        { 0x1F3, 0x3, STAT_UMF(0), MASK_UMF(0) },
        { 0x1F4, 0x3, STAT_UMF(1), MASK_UMF(1) },
        { 0x1F5, 0x3, STAT_UMF(2), MASK_UMF(2) },
        { 0x1F6, 0x3, STAT_UMF(3), MASK_UMF(3) },
};

/**
 * @brief Get CPU ID that program is currently running.
 *
 * @param None.
 *
 * @return CPU ID: Affinity level 0 - read and parse from MPIDR.
 */
static uint8_t Irq_GetCpuId(void);

/**
 * @brief Get GICR Address based on CPU is currently running.
 *
 * @param None.
 *
 * @return GICR Address.
 */
static uint32_t Irq_GetGICRAddr(void);

/**
 * @brief Get Affinity that indicate CPU ID and Cluster ID.
 *
 * @param None.
 *
 * @return Affinity (level 0 and level 1) read from MPIDR.
 */
static uint32_t Irq_GetAffinity(void);

void Irq_Setup(void)
{
	uint32_t rd, affinity, gicr_addr;
	affinity = Irq_GetAffinity();
	gicr_addr = Irq_GetGICRAddr();
	
	//
	// Configure the interrupt controller
	//
	// Set location of GIC
	R_GIC_SetAddr(CR52_GICD_ADDR, (void*)gicr_addr);
	
	// Enable GIC
	R_GIC_Enable();
	
	// Get the ID of the Redistributor connected to this PE
	rd = R_GIC_GetRedistID(affinity);
	// Mark this core as being active
	R_GIC_WakeUpRedist(rd);
	
	// Configure the CPU interface
	// This assumes that the SRE bits are already set
	R_GIC_SetPriorityMask(0xFF);
	R_GIC_EnableGroup0Ints();
	R_GIC_EnableGroup1Ints();
}

/* Set up a CR7 or INTC-RT GIC entry */
void Irq_SetupEntry(unsigned int id, IrqHandlerFn Handler, Context_t *Context)
{
	int ret;
	/* Just in case... */
	if (id > MAX_IRQ_NUMBER)
		while (1)
			;

	ret = Irq_MergeSetup(id);

	HandlerTable[id].Handler = Handler;
	HandlerTable[id].Context = Context;

	Irq_SetPriority(id, DEFAULT_ISR_PRIORITY);
}

static void StubHandler(void *data)
{
	while (1)
		;
}

/* Remove a CR7 or INTC-RT GIC entry */
void Irq_RemoveEntry(unsigned int id)
{
	/* Just in case... */
	if (id > MAX_IRQ_NUMBER)
		while (1)
			;

	Irq_Disable(id);

	HandlerTable[id].Handler = &StubHandler;
	HandlerTable[id].Context = NULL;
}

void Irq_Enable(unsigned int id)
{
    uint32_t rd, affinity;

    affinity = Irq_GetAffinity();
    R_GIC_SetIntRoute(id, 0, affinity);
    rd = R_GIC_GetRedistID(affinity);
    R_GIC_SetIntGroup(id, rd, GICV3_GROUP1_NON_SECURE);
    R_GIC_EnableInt(id, rd);
}
/* Legacy: IRQ_Enable should not be used */
int32_t IRQ_Enable (IRQn_ID_t irqn)
{
    Irq_Enable(irqn);
    return 0;
}

void Irq_Disable(unsigned int id)
{
	/* Same redistributor-indexing fix as Irq_SetPriority() below. */
	uint32_t rd = R_GIC_GetRedistID(Irq_GetAffinity());
	R_GIC_DisableInt(id, rd);
}
/* Legacy: IRQ_Disable should not be used */
int32_t IRQ_Disable (IRQn_ID_t irqn)
{
	Irq_Disable(irqn);
	return 0;
}

void Irq_SetPriority(unsigned int id, uint8_t priority)
{
    /* Was: R_GIC_SetIntPriority(id, Irq_GetCpuId(), priority). That indexed
     * gic_rdist[] with the MPIDR Aff0, but gic_rdist is seeded with this
     * core's OWN GICR frame address (R_GIC_SetAddr() gets Irq_GetGICRAddr()),
     * so the per-core offset was applied twice: on any core with Aff0 != 0
     * an ID < 32 write landed in another core's frame, or was dropped by
     * R_GIC_SetIntPriority()'s rd > gic_max_rd guard -- either way this
     * core's own SGI/PPI priority byte was never written by this call.
     * R_GIC_GetRedistID() instead
     * returns the index whose GICR_TYPER affinity matches this core's own
     * MPIDR Aff0 -- the same derivation Irq_Enable() uses -- which is
     * correct for every Aff0 value, including 0. */
    uint32_t rd = R_GIC_GetRedistID(Irq_GetAffinity());
    R_GIC_SetIntPriority(id, rd, priority);
}


/* CR52 GIC interrupt handler that also checks the INTC-RT GIC */
void vApplicationIRQHandler(uint32_t ulICCIAR)
{
	/*
	 * Interrupts cannot be re-enabled until the source of the interrupt is
	 * cleared. The ID of the interrupt is obtained by bitwise ANDing the ICCIAR
	 * value with 0x3FF
	 */
	uint32_t id = ulICCIAR & 0x3FFU;
	IvtEntry *pEntry;
	UBaseType_t UxSavedInterruptStatus;
	if (id > MAX_IRQ_NUMBER) {
        return;
	}

	pEntry = &HandlerTable[id];

	if (!pEntry->Handler) {
		/* No interrupt handler! */
		while (1)
			;
	}

    int channel_info = Irq_GetMergeStatReg(id);
	if (pEntry->Context && channel_info >= 0) {
        pEntry->Context->channel_info = channel_info;
	}
	UxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();
	pEntry->Handler(pEntry->Context);
	taskEXIT_CRITICAL_FROM_ISR(UxSavedInterruptStatus);
}

int Irq_GetTableId(unsigned int id)
{
	for (unsigned int i = 0 ; i < sizeof(r8a78000_irq_table) / sizeof(r8a78000_irq_table[0]); i++)
	{
		if (id == r8a78000_irq_table[i].irq)
			return i;
	}

	/* The irq id is not a merged interrupt */
	return -1;
}

uint32_t Irq_RegRead(uint32_t addr)
{
	return *((volatile uint32_t *)addr);
}

void Irq_RegWrite(uint32_t addr, uint32_t val)
{
	*((volatile uint32_t *)addr) = val;
}

int Irq_MergeSetup(unsigned int id)
{
	int t_id;
	uint32_t val, current_val = 0;
    uint32_t timeout = 1000;

	t_id = Irq_GetTableId(id);
	if (t_id < 0)
		return t_id;

	val = Irq_RegRead(r8a78000_irq_table[t_id].mask_reg);
	val &= ~r8a78000_irq_table[t_id].mask_val;
	Irq_RegWrite(r8a78000_irq_table[t_id].mask_reg, val);

    // Check whether the register is reflected setting value.
    while (timeout-- && (current_val != val)) {
        current_val = Irq_RegRead(r8a78000_irq_table[t_id].mask_reg);
    }

    if (timeout == 0)
        printf("Merge interrupt: Setup fail");

	return 0;
}

int Irq_GetMergeStatReg(unsigned int id)
{
	int t_id;

	t_id = Irq_GetTableId(id);
	if (t_id < 0)
		return t_id;

	return Irq_RegRead(r8a78000_irq_table[t_id].status_reg);
}

int Irq_SetIntType(unsigned int id, r_irq_type type)
{
    return R_GIC_SetIntType(id, 0, type);
}

static uint8_t Irq_GetCpuId(void)
{
        uint32_t mpidr = __get_MPIDR();
        uint8_t cpu_id = mpidr & 0xFF;
        return cpu_id;
}

static uint32_t Irq_GetGICRAddr(void)
{
	uint8_t cpu_id = Irq_GetCpuId();
	uint32_t gicr_addr = CR52_GIC_BASE_ADDR + gicr_offset_table[cpu_id];
	return gicr_addr;

}

static uint32_t Irq_GetAffinity(void)
{
        uint32_t mpidr = __get_MPIDR();
        uint32_t affinity = mpidr & 0xFFFF; // Get Affinity level 0 & 1
        return affinity;
}
