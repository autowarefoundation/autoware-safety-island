/*
 * Copyright (C) 2019-2020 Renesas Electronics Europe Ltd. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 */
/* Task 2: patched copy of
 * rcar_bsp/FreeRTOS/Demo/R-Car_Gen5_CR52/common/system_rcar_gen5.c at
 * submodule pin rcar-v2.5.0 (f129675e3). It shadows that file's object
 * inside libfreertos_bsp.a -- see the X5H_BSP_PATCHED_SOURCES comment in
 * ../CMakeLists.txt for the mechanism and for why the submodule itself is
 * not edited. The only deviation from the vendor file is one added
 * MPU_SetRegion() call in Init_MPU() covering the demo boot role's
 * cr52_ram1@5da00000 carveout, which sits outside every row of
 * RCAR_MEMMORY_ARR. Keep everything else byte-identical to the vendor file
 * so a diff against the submodule shows exactly the addition. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "portmacro.h"
#include "interrupts.h"
#include "board.h"
#include "cmsis_rcar_gen5.h"
#include "mpu.h"
#include "state-manager/r_state_manager.h"
#include "memory_map/memory_map.h"
#include "tcm.h"
#include "serial/r_serial.h"
#include "pfc/r_pfc_api.h"

#define CNTCR_ADDR   ((volatile uint32_t *)0x1C000000) // Counter Control Register
#define SILENT_CONSOLE_ON (1U)

extern const unsigned int __bss_start__;
extern const unsigned int __bss_end__;
extern const unsigned int _STACK_SIZE;

extern char _RAM_START;
extern const uint32_t _RAM_SIZE;
extern const uint32_t _TCM_SIZE;

extern uint32_t __tcm_start__, __tcm_end__;
extern const uint32_t __kernel_region_start__, __kernel_region_end__;
static int is_linker_tcm_symbols_define = 0;

extern uint32_t __CONFIG_SILENT_CONSOLE__ __attribute__((weak)); /* Weak linker symbol for log control, NULL if not defined in linker script */
extern uint32_t _Reset;
uint32_t resource_table;
#if ETHER_ENABLE
extern uint32_t eth_non_cache_start;
#endif

#if RAM_CONSOLE_ENABLE
char ram_console[1024] = "";
#endif

extern int main(void);

extern void __libc_init_array(void) ;

static int init_linker_symbols_check(void)
{
#if (TCM_ENABLE == 1)
    if ((__kernel_region_start__ != 0u) && (__kernel_region_end__ != 0u) &&
        (__tcm_start__ != 0u) && (__tcm_end__ != 0u) &&
        (uint32_t)&_TCM_SIZE != 0u &&
        ((uint32_t)&__kernel_region_end__ > (uint32_t)&__kernel_region_start__) &&
        ((uint32_t)&__tcm_end__ > (uint32_t)&__tcm_start__))
    {
        return 1;
    }
    return 0;
#else
    return 0;
#endif
}

static void Init_MPU(void)
{
//    uint32_t entry_address = (uint32_t) &_RAM_START;
    /* Disable MPU */
//    MPU_Disable();

    MPU_Init();

    is_linker_tcm_symbols_define = init_linker_symbols_check();

#if (TCM_ENABLE == 1)    
    if (is_linker_tcm_symbols_define == 1) 
    {
        MPU_SetRegion(REGION_SRAM_ATTR((uint32_t) &__kernel_region_start__, (uint32_t) &__kernel_region_end__ - (uint32_t) &__kernel_region_start__));
        MPU_SetRegion(REGION_TCM_ATTR((uint32_t) &__tcm_start__, (uint32_t) &_TCM_SIZE));
        MPU_SetRegion(REGION_SRAM_ATTR((uint32_t) &__tcm_end__, (uint32_t) &_RAM_SIZE - ((uint32_t) &__tcm_end__ - (uint32_t) &__kernel_region_start__)));
    }
    else
    {
        MPU_SetRegion(REGION_SRAM_ATTR((uint32_t) &_RAM_START, (uint32_t) &_RAM_SIZE));
    }
#else
    MPU_SetRegion(REGION_SRAM_ATTR((uint32_t) &_RAM_START, (uint32_t) &_RAM_SIZE));
#endif

    for (int i = 0; i < sizeof(RCAR_MEMMORY_ARR)/sizeof(st_memory_region_t); i++) {
       
        uint8_t ret = 0;

        // Avoid duplicating execution and IO memory.
        if (RCAR_MEMMORY_ARR[i].mem_addr.base_address == (unsigned int)(uintptr_t)&_RAM_START) {
            continue;
        }

        switch (RCAR_MEMMORY_ARR[i].attr) {
            case DEVICE_ATTR:
                ret = MPU_SetRegion(REGION_DEVICE_ATTR(RCAR_MEMMORY_ARR[i].mem_addr.base_address, RCAR_MEMMORY_ARR[i].mem_addr.size));
                break;

            case RAM_ATTR:
                ret = MPU_SetRegion(REGION_RAM_ATTR(RCAR_MEMMORY_ARR[i].mem_addr.base_address, RCAR_MEMMORY_ARR[i].mem_addr.size));
                break;

            case RAM_NOCACHE_ATTR:
                ret = MPU_SetRegion(REGION_RAM_NOCACHE_ATTR(RCAR_MEMMORY_ARR[i].mem_addr.base_address, RCAR_MEMMORY_ARR[i].mem_addr.size));
                break;

            case RAM_TEXT_ATTR:
                ret = MPU_SetRegion(REGION_RAM_TEXT_ATTR(RCAR_MEMMORY_ARR[i].mem_addr.base_address, RCAR_MEMMORY_ARR[i].mem_addr.size));
                break;

            case RAM_RO_ATTR:
                ret = MPU_SetRegion(REGION_RAM_RO_ATTR(RCAR_MEMMORY_ARR[i].mem_addr.base_address, RCAR_MEMMORY_ARR[i].mem_addr.size));
                break;

            case SRAM_ATTR:
                ret = MPU_SetRegion(REGION_SRAM_ATTR(RCAR_MEMMORY_ARR[i].mem_addr.base_address, RCAR_MEMMORY_ARR[i].mem_addr.size));
                break;

            case FLASH_ATTR:
                ret = MPU_SetRegion(REGION_FLASH_ATTR(RCAR_MEMMORY_ARR[i].mem_addr.base_address, RCAR_MEMMORY_ARR[i].mem_addr.size));
                break;

            case TCM_ATTR:
                ret = MPU_SetRegion(REGION_TCM_ATTR(RCAR_MEMMORY_ARR[i].mem_addr.base_address, RCAR_MEMMORY_ARR[i].mem_addr.size));
                break;

            default:
#if RAM_CONSOLE_ENABLE
                snprintf(ram_console + strlen(ram_console), sizeof(ram_console) - strlen(ram_console), "Set MPU region index %d FAIL. Memory attribute isn't supported;", i + 1);
#endif
			    break;
        }

        if (ret) {
#if RAM_CONSOLE_ENABLE
            snprintf(ram_console + strlen(ram_console), sizeof(ram_console) - strlen(ram_console), "Set MPU region index %d FAIL. Exceeded number of MPU regions supported;", i + 1);
#endif
        } 
    } 

    /* CES 2027 demo role: the four carveouts cr52_1 lists all live here,
     * outside every region in RCAR_MEMMORY_ARR. Same attribute as the
     * vendor's shared-memory rows. One region covers all four:
     *
     *   0x5da00000 +0x200000  cr52_ram1, holding our .resource_table
     *   0x5dc00000 +0x3000    vdev0vring0
     *   0x5dc03000 +0x3000    vdev0vring1
     *   0x5dc10000 +0x100000  vdev0buffer
     *
     * 4 MiB, not the 2 MiB this started as: the vrings and the rpmsg buffer
     * pool sit above cr52_ram1, and Linux allocates them from those three
     * carveouts only because openadkit's make-demo-dtb.sh names the device
     * tree nodes vdev0vring0/1 and vdev0buffer. Without those names
     * remoteproc allocated all three from linux,cma@40000000 -- an address no
     * row of RCAR_MEMMORY_ARR maps, since the vendor table expects Linux CMA
     * at LINUX_CMA_ADDRESS_0 (0xa2600000). That is what aborted this firmware
     * in rpmsg_init_vdev at gate D1b on board 2, 2026-09-17. Keep this region
     * and that script's layout in step: the resource table publishes
     * FW_RSC_ADDR_ANY and takes back whatever addresses Linux writes into it,
     * so a carveout outside this window is a data abort, not a diagnostic. */
    if (MPU_SetRegion(REGION_RAM_NOCACHE_ATTR(0x5da00000u, 0x400000u))) {
#if RAM_CONSOLE_ENABLE
        snprintf(ram_console + strlen(ram_console), sizeof(ram_console) - strlen(ram_console), "Set MPU region for the demo carveout FAIL. Exceeded number of MPU regions supported;");
#endif
    }

    /* Enable MPU */
    MPU_Enable();
}

__STATIC_INLINE void bss_init(unsigned int* section_begin, unsigned int* section_end)
{
  // Iterate and clear word by word.
  // It is assumed that the pointers are word aligned.
  unsigned int *p = section_begin;
  while (p < section_end)
    *p++ = 0;
}

static void FPU_Enable(void)
{
#define BSP_CPCAR_CP_ENABLE             (0x00F00000)
#define BSP_FPEXC_EN_ENABLE             (0x40000000)
    uint32_t apacr;
    uint32_t fpexc;

    /* Enables cp10 and cp11 accessing */
    apacr  = __get_CPACR();
    apacr |= BSP_CPCAR_CP_ENABLE;
    __set_CPACR(apacr);
    __ISB();

    /* Enables the FPU */
    fpexc  = __get_FPEXC();
    fpexc |= BSP_FPEXC_EN_ENABLE;
    __set_FPEXC(fpexc);
    __ISB();

}

/* Enable data and instruction cache in SVC mode */
void EnableCache()
{
    uint32_t reg_val = 0;

    // Invalidate all Instruction Caches to PoU (ICIALLU)
    __asm__ volatile ("mcr p15, 0, %0, c7, c5, 0" : : "r"(0) : "memory");

    // Invalidate all entries from branch predictors (BPIALL)
    __asm__ volatile ("mcr p15, 0, %0, c7, c5, 6" : : "r"(0) : "memory");

    // Read System Control Register (SCTLR)
    __asm__ volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(reg_val) : : "memory");

    // instruction cache enable (SCTLR.I), data cache enable (SCTLR.C)
    reg_val |= BIT(12) | BIT (2);

    // Enabled instruction and data cache (SCTLR)
    __asm__ volatile ("mcr p15, 0, %0, c1, c0, 0" : : "r"(reg_val) : "memory");

    __DSB();
    __ISB();
}

static void EnablePMU(void)
{
    uint32_t value;

    // Enable PMU and reset event and cycle counters
    value = (1 << 0)  // E: All counters are enabled.
          | (1 << 2); // C: Reset cycle counter.
    __asm__ volatile ("mcr p15, 0, %0, c9, c12, 0" :: "r"(value));

    // Enable cycle counter (counter 31)
    value = (1 << 31);
    __asm__ volatile ("mcr p15, 0, %0, c9, c12, 1" :: "r"(value));
}

void SystemInit(void)
{
    bss_init((unsigned int *)&__bss_start__, (unsigned int *)&__bss_end__);
#if (defined(__FPU_USED) && (__FPU_USED == 1U))
    FPU_Enable();
#endif
    Init_MPU();

#if (TCM_ENABLE == 1)   
    if (is_linker_tcm_symbols_define == 1) 
    {
        st_memory_t info_osal = R_UTILS_GetMemoryRegionInfo(OSAL, 0);
        volatile uint32_t *osal_mem = (volatile uint32_t *)info_osal.base_address;
        memcpy((void *)osal_mem, &__tcm_start__, (size_t)&_TCM_SIZE);

        // Configuration for TCM region B.
        ConfigureTCM(RCAR_TCM_B, (uint32_t)&__tcm_start__, RCAR_TCM_SIZE_32KB);

        // Enable TCM region B at EL1.
        ControlTCM(RCAR_TCM_B, RCAR_TCM_EL1, RCAR_TCM_ENABLE);

        memcpy(&__tcm_start__, (void *)osal_mem, (size_t)&_TCM_SIZE);
        memset((void *)osal_mem, 0, (size_t)&_TCM_SIZE);
    }
#endif

#if (CACHE == 1)
    EnableCache(); 
#endif
    EnablePMU();
    __libc_init_array();
    portDISABLE_INTERRUPTS();
    *CNTCR_ADDR = 1;    /* enable system counter */

    /* Init UART */
    (void)R_SERIAL_PortInit(UART_ID);
    if ((uint32_t)&__CONFIG_SILENT_CONSOLE__ == SILENT_CONSOLE_ON)
    {
        R_SERIAL_SetLogState(LOG_OFF);
    }

    Irq_Setup();
    if (R_StateManager_Init()) {
        printf("Error: Failed to init State Manager.\r\n");
        return;
    }
}

void assert_func(const char *file, int line, const char *func)
{
    printf("ASSERT! File \"%s\", Line \"%d\", Function \"%s\" \n", file, line, func);
    for (;;)
    {
        __BKPT(0);
    }
}
