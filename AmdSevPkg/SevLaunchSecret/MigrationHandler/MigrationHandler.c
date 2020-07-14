#include <Library/UefiLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseLib.h> 
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/MemoryAllocationLib.h>

// This won't build
// #include <stdio.h>

// This is from Linux kernel: arch/x86/include/asm/ptrace.h
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

// This is from Linux kernel: arch/x86/include/asm/desc_defs.h
struct desc_ptr {
	unsigned short size;
	unsigned long address;
} __attribute__((packed)) ;

//
// Variables used by RestoreState.nasm
//
UINT64 gSavedRIP;
UINT64 gSavedCR0;
UINT64 gSavedCR2;
UINT64 gSavedCR3;
UINT64 gSavedCR4;
struct desc_ptr gSavedGDTDesc;
struct pt_regs gSavedContext;

//
// Each level has 512 entries of 64-bit each (which together fit in one 4096-byte page)
//
#define ENTRIES (EFI_PAGE_SIZE / sizeof(UINT64))
extern UINT64 pgd[ENTRIES];
extern UINT64 pud[ENTRIES];
extern UINT64 pmd[ENTRIES];
extern UINT64 pte[ENTRIES];

// Mailbox struct for communicating with VMM
struct sev_mh_params {
    unsigned long nr;
    unsigned long gpa;
    int do_prefect;
    int ret; 
    int go;
    int done;
};

void MyTarget(void) {
  DEBUG((DEBUG_ERROR,"MIGRATION HANDLER - inside MyTarget 111\n"));
}

static void SetCPUState()
{
  gSavedContext.ax    = 0xffffffff886f2f20;
  gSavedContext.bx    = 0x0;
  gSavedContext.cx    = 0x1;
  gSavedContext.dx    = 0x236e;
  gSavedContext.si    = 0x87;
  gSavedContext.di    = 0x0;
  gSavedContext.bp    = 0xffffffff89203e20;
  gSavedContext.sp    = 0xffffffff89203e00;
  gSavedContext.r8    = 0x0000000c4ff4c4e3;
  gSavedContext.r9    = 0x0000000000000200;
  gSavedContext.r10   = 0x0;
  gSavedContext.r11   = 0x0;
  gSavedContext.r12   = 0x0;
  gSavedContext.r13   = 0xffffffff89213840;
  gSavedContext.r14   = 0x0;
  gSavedContext.r15   = 0x0;
  gSavedContext.flags = 0x246;
  gSavedContext.ip    = 0xffffffff886f330e; // linux top ?
  //gSavedContext.ip    = 0x7f6b0a481d26; // grub?
  //gSavedContext.ip    = (unsigned long)(MyTarget); // 0x7f6b0a481d26;
  gSavedContext.cs    = 0x10;
  gSavedContext.ss    = 0x18;

  gSavedRIP = gSavedContext.ip;
  gSavedCR0 = 0x80050033;
  gSavedCR2 = 0x000055fc38529000;
  gSavedCR3 = 0x0000000037430000;
  gSavedCR4 = 0x3406f0;

  gSavedGDTDesc.address = 0xfffffe0000001000;
  gSavedGDTDesc.size = 0x0000007f;
}

//void* GetPage(void) {
EFI_PHYSICAL_ADDRESS GetPage(void) {
  DebugPrint(DEBUG_ERROR, "MIGRATION HANDLER GetPage start\n");
  //void* page = AllocateAlignedPages (1, BASE_4KB);
  EFI_PHYSICAL_ADDRESS addr;
  EFI_STATUS Status;
  Status = gBS->AllocatePages (AllocateAnyPages, EfiBootServicesData, 1, &addr);
  ASSERT_EFI_ERROR (Status);
  // TODO: ZeroMem(page, SIZE_4KB);
  return addr;
}

void PrepareMemory(void) {
  //EFI_PHYSICAL_ADDRESS pmd = GetPage();
  //EFI_PHYSICAL_ADDRESS pud = GetPage();
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER PrepareMemory pgd = %p\n", pgd);
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER PrepareMemory pud = %p\n", pud);
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER PrepareMemory pmd = %p\n", pmd);
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER PrepareMemory pte = %p\n", pte);
}

// Defined in RestoreState.nasm
void RestoreRegisters(void);

EFI_STATUS
EFIAPI
MigrationHandlerMain(
  IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
  )
{
  UINT64 params_base = PcdGet32(PcdSevMigrationMailboxBase); 
  volatile struct sev_mh_params *params = (void *) params_base;   
  

  // this doesn't seem to do anything? 
  // maybe prints to a log somewhere
  DEBUG((DEBUG_ERROR,"MIGRATION HANDLER 111\n")); 
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER 222\n"); 

  // this requires stdio (see above)
  //printf("MIGRATION HANDLER\n");

  // This causes a nice panic.
  //Print(L"MIGRATION HANDLER\n"); 

  //DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER MyTarget = %016x\n", (unsigned long)MyTarget);

  SetCPUState();
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER After SetCPUState 222.8 gSavedRIP = %016lx\n", gSavedRIP);
  PrepareMemory();
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER After PrepareMemory 333 RestoreRegisters = %p\n", RestoreRegisters);
  RestoreRegisters();

  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER After RestoreRegisters 444\n");
  return 0;

  while(1) {
    
    // wait for command
    while(!params->go);
    
    // handle commands
    switch (params->nr){
      case 0:
        // do something 
        break;

    }


    params->go = 0;
    params->done = 1;
         
  }
  return 0;
}


// seems to start here no matter how ENTRY_POINT 
// is set in INF
EFI_STATUS 
EFIAPI
_ModuleEntryPoint(
IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
  )
{
  return MigrationHandlerMain(ImageHandle, SystemTable);
}

