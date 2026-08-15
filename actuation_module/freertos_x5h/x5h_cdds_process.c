// Copyright (c) 2026, Arm Limited and contributors.
// SPDX-License-Identifier: Apache-2.0
//
// Task 19: the fix for the defect that was killing the CR52 silently, plus
// the one console line that proves the fix worked for the stated reason.
//
// ---- the defect
//
// CycloneDDS's FreeRTOS port of ddsrt_getprocessname()
// (cyclonedds/src/ddsrt/src/process/freertos/process.c:22-26) is
//
//     char *ddsrt_getprocessname(void)
//     { return pcTaskGetName(xTaskGetCurrentTaskHandle()); }
//
// pcTaskGetName() returns &pxTCB->pcTaskName[0] -- a BORROWED interior
// pointer into the live FreeRTOS TCB. It does not copy. The rest of
// CycloneDDS expects an OWNED string back.
//
// That ownership claim is worth stating carefully rather than asserting,
// because the header does not spell it out in so many words. Three things
// establish it, none of them ambiguous:
//   - the doc comment (cyclonedds/src/ddsrt/include/dds/ddsrt/process.h:61-71)
//     promises "Falls back to process-{pid} on failure", which can only be a
//     freshly formatted string -- a caller cannot treat that as borrowed;
//   - both other ports return an owned copy on every path: POSIX ends in
//     ddsrt_strdup()/ddsrt_asprintf() (process/posix/process.c:41-85), and
//     Windows likewise (process/windows/process.c:25-35);
//   - the sole caller in this image frees it --
//
//     cyclonedds/src/core/ddsi/src/ddsi_init.c:1299  procname = ddsrt_getprocessname();
//     cyclonedds/src/core/ddsi/src/ddsi_init.c:1304  ddsrt_free(procname);
//
// That free() is correct and stays. ddsrt_free()
// (cyclonedds/src/ddsrt/src/heap/freertos/heap.c:138-144) subtracts the
// allocator's header offset and calls vPortFree(), which on this target is
// heap_useNewlib.c's `free(pv)` (rcar_bsp/.../common/heap_useNewlib.c:194-196).
// So newlib was handed free(pxTCB + 44) and read TCB fields as a chunk header.
//
// Every step of that is confirmed in the linked ELF rather than argued from
// the source. Disassembling _free_r (0x1175bb3c in the Task 18 image):
//
//     +0x18  ldr r0, [r4, #-4]     size word,      here pxTCB+40
//     +0x20  sub ip, r4, #8        chunk = mem-8,  here pxTCB+36
//     +0x24  bic r1, r0, #1        drop PREV_INUSE
//     +0x3c  tst r0, #1            PREV_INUSE clear -> backward consolidation
//     +0x4c  ldr r4, [r4, #-8]     prev_size,      here pxTCB+36
//     +0x50  sub ip, ip, r4        ip = (pxTCB+36) - prev_size
//     +0x58  ldr r4, [ip, #8]      <-- the faulting load
//
// pxTCB+40 is xEventListItem.pxContainer, which is NULL for a running task,
// so PREV_INUSE is clear and the backward-consolidation path is taken.
// pxTCB+36 is xEventListItem.pvOwner, which is pxTCB itself. The subtraction
// therefore cancels the TCB's address entirely: ip = 36, and [ip, #8] reads
// address 44 = 0x2C. That is why the Task 18 capture's DFAR=0x2c is a
// structural constant of the TCB layout rather than an address, and why the
// wedge reproduced identically on all three boots.
//
// Why it never showed on S32Z2: that target links heap_4.c, whose vPortFree()
// checks the block's allocated bit, finds it clear, and returns. Same latent
// defect, no symptom. X5H links heap_useNewlib.c -> newlib free(), which has
// no such check.
//
// ---- the fix, without touching the vendored tree
//
// cyclonedds/ is frozen, so the corrected function is defined HERE and
// pre-empts the vendored one at link time. The mechanism is the one Task 18
// already proved on this target for abort()/_exit() (see x5h_diag.c's R2
// section): this file is an ordinary object linked directly into
// actuation_x5h, so its definitions are in the linker's symbol table before
// libddsc.a is ever scanned, and an archive member is only pulled in to
// satisfy a symbol that is still undefined.
//
// There is one non-obvious step, and getting it wrong is a link error rather
// than a silent miss, which is the good direction: the vendored
// process.c.obj defines TWO symbols, ddsrt_getpid and ddsrt_getprocessname
// (confirmed with `arm-none-eabi-nm libddsc.a`), and ddsrt_getpid is
// referenced from elsewhere inside the archive -- before this file existed,
// actuation_x5h.map recorded process.c.obj being pulled in by random.c.obj
// for exactly that symbol. Defining only ddsrt_getprocessname here would
// still drag the member in and collide with it. So this file defines BOTH,
// which leaves process.c.obj with no undefined symbol left to satisfy and
// keeps it out of the link entirely. The verification for that is
// `process.c.obj` being absent from actuation_x5h.map.
//
// ddsrt_getpid() below is therefore a deliberate byte-for-byte restatement of
// the vendored one, not an improvement on it. It must stay that way.
//
// ---- P1: the console line is a test, not decoration
//
// x5h-diag: procname ... is printed once, from inside the corrected function,
// and a reader is meant to check it rather than skim it. It carries the TCB
// address, the borrowed pointer, the owned copy, delta = borrow - tcb, the
// two words newlib would have read as a chunk header, whether the borrowed
// pointer is inside the allocator's own [HeapBase, HeapLimit), and the
// address a free() of the borrowed pointer would have been handed.
//
// Expected on a correct diagnosis:
//
//     delta=52                 pcTaskName's offset in this build's TCB. From
//                              tasks.c:358-378 with this FreeRTOSConfig.h:
//                              pxTopOfStack 4, no MPU wrappers, single core,
//                              xStateListItem 20, xEventListItem 20,
//                              uxPriority 4, pxStack 4 -> 52.
//     hdr_size=0x0             xEventListItem.pxContainer of a running task.
//                              PREV_INUSE clear is what selects the faulting
//                              path; any non-zero value here means the fault
//                              came from somewhere else.
//     hdr_prev=<tcb>           xEventListItem.pvOwner == the TCB. This is the
//                              value that cancels, and it is the single most
//                              load-bearing number on the line.
//     free_would_have_hit      = tcb+44, i.e. borrow - 8.
//
// From hdr_prev and free_would_have_hit alone, 0x2C is re-derivable:
// ip = (free_would_have_hit - 8) - hdr_prev, and the faulting load is
// [ip, #8]. If delta is not 52, or hdr_size is not 0, or hdr_prev is not the
// TCB address, the diagnosis in this comment is wrong and the line says so.
//
// borrow_in_heap is the one field that is easy to read backwards, so read it
// deliberately: the expected value is 1, and 1 means the diagnosis holds.
//
// It is 1 by construction, not by accident. A TCB created through
// xTaskCreate() comes from pvPortMalloc(), which on this target is plain
// malloc() (heap_useNewlib.c:190-193) served out of [HeapBase, HeapLimit) by
// that same file's _sbrk_r() (heap_useNewlib.c:112-141). The borrowed pointer
// is an INTERIOR pointer into a LIVE heap block -- not a pointer outside the
// heap. A 0 here would mean the name did not come from a dynamically created
// task's TCB at all, and is the surprise worth chasing.
//
// That has a consequence worth writing down where the next person will look
// for it, because the obvious hardening is the wrong one: a bounds check of
// ddsrt_free()'s argument against [HeapBase, HeapLimit) would NOT have caught
// this defect. Both the borrowed pointer and borrow-8 are inside that range,
// so such a guard passes and the free proceeds exactly as before. Discriminating
// this fault needs the chunk header at borrow-8-4 to be sanity-checked, not the
// address range -- which is a different and considerably less safe proposition
// (see x5h_diag.h on why nothing here may walk newlib's arena).

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "FreeRTOS.h"
#include "task.h"

/* ddsrt_pid_t (== TaskHandle_t under DDSRT_WITH_FREERTOS) and the two
   prototypes this file overrides. Included rather than hand-declared so a
   future signature change in the vendored header is a compile error here
   instead of a silently mismatched definition. */
#include "dds/ddsrt/process.h"
/* ddsrt_strdup(). */
#include "dds/ddsrt/string.h"

/* x5h_diag_puts() and the integer writers: the same non-allocating, non-
   scheduler-locking console path the rest of the diagnostic surface uses.
   See x5h_diag.h for why they are shared rather than re-rolled. */
#include "x5h_diag.h"

/* The allocator's own linker bounds, declared exactly as heap_useNewlib.c:105
   and x5h_diag.c declare them -- as `char` objects whose ADDRESS is the
   value. */
extern char HeapBase, HeapLimit;

/* ddsrt_free()'s header offset, taken from the expression at
   cyclonedds/src/ddsrt/src/heap/freertos/heap.c:31-32 rather than from a
   remembered constant, so the two cannot drift. This file only reports the
   offset; it does not allocate or free with it.

   The assertion is not paranoia about the arithmetic, which is trivial. It
   pins the value that the ELF evidence in this file's header comment was
   read against: ddsrt_free() compiles to `sub r0, r0, #8` in the Task 18
   image, and every "borrow - 8", "pxTCB+44" and "0x2C" above is that 8. If
   portBYTE_ALIGNMENT ever moved, the whole derivation would need redoing,
   and this is where that has to be noticed. */
#define X5H_DDSRT_HEAP_OFST \
    ((sizeof(size_t) + portBYTE_ALIGNMENT - 1) & ~(size_t)(portBYTE_ALIGNMENT - 1))

_Static_assert(X5H_DDSRT_HEAP_OFST == 8U,
               "ddsrt_free()'s header offset is no longer 8 bytes. It is "
               "derived here from heap.c:31-32's own expression, so this "
               "means portBYTE_ALIGNMENT moved -- which invalidates the "
               "free()-lands-on-pxTCB+44 derivation in this file's header "
               "comment and the DFAR=0x2c reading of the Task 18 capture.");

/* Byte-for-byte the vendored ddsrt_getpid()
   (cyclonedds/src/ddsrt/src/process/freertos/process.c:16-20). Present only
   so that process.c.obj is never pulled out of libddsc.a -- see this file's
   header comment. Not a place to add behaviour. */
ddsrt_pid_t
ddsrt_getpid(void)
{
  return xTaskGetCurrentTaskHandle();
}

/* Reads a word that, on a correct diagnosis, lies inside the TCB. Checked
   rather than trusted, because this runs in the middle of reporting on a
   pointer whose provenance is exactly what is in question: if the diagnosis is
   wrong, the address handed in is wrong too, and a fault taken HERE would
   replace the evidence with a second, less informative one. Two rejects, both
   of them real hazards rather than decoration:

     - outside [HeapBase, HeapLimit), i.e. not a readable heap address;
     - not 4-byte aligned. borrow-16 is word-aligned only because delta is 52;
       a wrong delta can make it unaligned, and this build cannot assume an
       unaligned LDR is permitted.

   Either way the caller prints "<unreadable>" and the rest of the line, which
   still carries delta and free_would_have_hit, survives to be read. */
static bool x5h_read_heap_word(uintptr_t addr, uint32_t *out)
{
  if ((addr & 3U) != 0U ||
      addr < (uintptr_t)&HeapBase ||
      addr > (uintptr_t)&HeapLimit - sizeof(uint32_t)) {
    return false;
  }
  *out = *(const volatile uint32_t *)addr;
  return true;
}

static void x5h_put_word_or_note(uintptr_t addr)
{
  uint32_t v = 0;

  if (x5h_read_heap_word(addr, &v)) {
    x5h_diag_put_hex32(v);
  } else {
    x5h_diag_puts("<unreadable>");
  }
}

/* P1. One line, printed once, from inside the corrected function. How to read
   it is in this file's header comment; nothing is repeated here. */
static void x5h_report_borrowed_name(const void *tcb, const char *borrow,
                                     const char *copy)
{
  uintptr_t t = (uintptr_t)tcb;
  uintptr_t b = (uintptr_t)borrow;
  /* What ddsrt_free(borrow) would have handed to newlib's free(). */
  uintptr_t mem = b - X5H_DDSRT_HEAP_OFST;

  x5h_diag_puts("x5h-diag: procname tcb=");
  x5h_diag_put_hex32((uint32_t)t);
  x5h_diag_puts(" borrow=");
  x5h_diag_put_hex32((uint32_t)b);
  x5h_diag_puts(" copy=");
  x5h_diag_put_hex32((uint32_t)(uintptr_t)copy);
  x5h_diag_puts(" delta=");
  x5h_diag_put_i32((int32_t)(b - t));
  x5h_diag_puts(" borrow_in_heap=");
  x5h_diag_put_u32((b >= (uintptr_t)&HeapBase &&
                    b < (uintptr_t)&HeapLimit) ? 1U : 0U);
  x5h_diag_puts(" free_would_have_hit=");
  x5h_diag_put_hex32((uint32_t)mem);
  x5h_diag_puts(" hdr_prev=");
  x5h_put_word_or_note(mem - 8U);
  x5h_diag_puts(" hdr_size=");
  x5h_put_word_or_note(mem - 4U);
  x5h_diag_puts("\n");
}

/* R1. Returns an OWNED copy -- the ownership this file's header comment
   establishes from process.h:61-71 and the POSIX/Windows ports, and what
   ddsi_init.c:1304's ddsrt_free() requires.

   ddsrt_strdup() -> ddsrt_memdup() -> ddsrt_malloc_s()
   (cyclonedds/src/ddsrt/src/string.c:147-163), i.e. the NON-aborting
   allocator entry point: it returns NULL on exhaustion rather than calling
   abort(), and ddsi_init.c:1302 already guards the result on NULL. That is
   the whole reason ddsrt_strdup() is right here and ddsrt_malloc() would not
   be. */
char *
ddsrt_getprocessname(void)
{
  /* Static storage, not an automatic: the line is a one-shot proof, and
     reprinting it on a second domain creation would add noise to a console
     whose silence and whose content are both being read as evidence.
     Unsynchronised on purpose -- there is exactly one caller in this image
     (ddsi_init.c:1299), and the worst a race could do is print it twice. */
  static bool reported = false;

  TaskHandle_t self = xTaskGetCurrentTaskHandle();
  char *borrow = pcTaskGetName(self);
  char *copy = (borrow != NULL) ? ddsrt_strdup(borrow) : NULL;

  if (!reported && borrow != NULL) {
    reported = true;
    x5h_report_borrowed_name((const void *)self, borrow, copy);
  }

  return copy;
}
