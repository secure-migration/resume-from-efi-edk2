/** @file
  Platform PEI module include file.

  Copyright (c) 2006 - 2016, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef _PLATFORM_PEI_H_INCLUDED_
#define _PLATFORM_PEI_H_INCLUDED_

#include <IndustryStandard/E820.h>


// we should probbaly find a better home for this. 
// just dump it here for now
struct pt_regs {
/*
 * C ABI says these regs are callee-preserved. They aren't saved on kernel entry
 * unless syscall needs a complete, fully filled "struct pt_regs".
 */
        unsigned long r15;
        unsigned long r14;
        unsigned long r13;
        unsigned long r12;
        unsigned long bp;
        unsigned long bx;
/* These regs are callee-clobbered. Always saved on kernel entry. */
        unsigned long r11;
        unsigned long r10;
        unsigned long r9;
        unsigned long r8;
        unsigned long ax;
        unsigned long cx;
        unsigned long dx;
        unsigned long si;
        unsigned long di;
/*
 * On syscall entry, this is syscall#. On CPU exception, this is error code.
 * On hw interrupt, it's IRQ number:
 */
        unsigned long orig_ax;
/* Return frame for iretq */
        unsigned long ip;
        unsigned long cs;
        unsigned long flags;
        unsigned long sp;
        unsigned long ss;
/* top of stack page */
};

struct desc_ptr {
        unsigned short size;
        unsigned long address;
} __attribute__((packed));

#pragma pack(1)
struct cpu_state {
  char magic[8];
  UINT64 version;
  struct pt_regs regs;
  UINT64 rip;
  UINT64 cr0;
  UINT64 cr2;
  UINT64 cr3;
  UINT64 cr4;
  struct desc_ptr gdt_desc;
  char end_magic[8];
};
#pragma pack()

VOID
AddIoMemoryBaseSizeHob (
  EFI_PHYSICAL_ADDRESS        MemoryBase,
  UINT64                      MemorySize
  );

VOID
AddIoMemoryRangeHob (
  EFI_PHYSICAL_ADDRESS        MemoryBase,
  EFI_PHYSICAL_ADDRESS        MemoryLimit
  );

VOID
AddMemoryBaseSizeHob (
  EFI_PHYSICAL_ADDRESS        MemoryBase,
  UINT64                      MemorySize
  );

VOID
AddMemoryRangeHob (
  EFI_PHYSICAL_ADDRESS        MemoryBase,
  EFI_PHYSICAL_ADDRESS        MemoryLimit
  );

VOID
AddReservedMemoryBaseSizeHob (
  EFI_PHYSICAL_ADDRESS        MemoryBase,
  UINT64                      MemorySize,
  BOOLEAN                     Cacheable
  );

VOID
AddressWidthInitialization (
  VOID
  );

VOID
Q35TsegMbytesInitialization (
  VOID
  );

VOID
Q35SmramAtDefaultSmbaseInitialization (
  VOID
  );

EFI_STATUS
PublishPeiMemory (
  VOID
  );

UINT32
GetSystemMemorySizeBelow4gb (
  VOID
  );

VOID
QemuUc32BaseInitialization (
  VOID
  );

VOID
InitializeRamRegions (
  VOID
  );

EFI_STATUS
PeiFvInitialization (
  VOID
  );

VOID
MemTypeInfoInitialization (
  VOID
  );

VOID
InstallFeatureControlCallback (
  VOID
  );

VOID
InstallClearCacheCallback (
  VOID
  );

EFI_STATUS
InitializeXen (
  VOID
  );

BOOLEAN
XenDetect (
  VOID
  );

VOID
AmdSevInitialize (
  VOID
  );

extern BOOLEAN mXen;

VOID
XenPublishRamRegions (
  VOID
  );

extern EFI_BOOT_MODE mBootMode;

extern BOOLEAN mS3Supported;

extern UINT8 mPhysMemAddressWidth;

extern UINT32 mMaxCpuCount;

extern UINT16 mHostBridgeDevId;

extern BOOLEAN mQ35SmramAtDefaultSmbase;

extern UINT32 mQemuUc32Base;

#endif // _PLATFORM_PEI_H_INCLUDED_
