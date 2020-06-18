#include <Library/UefiLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseLib.h> 
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>

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
UINT64 gSavedCR3;
UINT64 gSavedCR4;
struct desc_ptr gSavedGDT;
struct pt_regs gSavedContext;

// Mailbox struct for communicating with VMM
struct sev_mh_params {
    unsigned long nr;
    unsigned long gpa;
    int do_prefect;
    int ret; 
    int go;
    int done;
};

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
  DEBUG((DEBUG_ERROR,"MIGRATION HANDLER\n")); 
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER\n"); 

  // this requires stdio (see above)
  //printf("MIGRATION HANDLER\n");

  // This causes a nice panic.
  //Print(L"MIGRATION HANDLER\n"); 

  // put some state into regs for testing
  // // this is supposed to jump into grub....
  gSavedContext.ax = 0xfffffffffffffdfe;
  gSavedContext.bx = 0x5642b81a0e90;
  gSavedContext.cx = 0x7f6b0a481d26;
  gSavedContext.dx = 0x7ffdef1adf40;
  gSavedContext.si = 0xd;
  gSavedContext.di = 0x5642b84a7d70;
  gSavedContext.bp = 0x7ffdef1adfb0;
  gSavedContext.sp = 0x7ffdef1adf20;
  gSavedContext.r8 = 0x8;
  gSavedContext.r9 = 0;
  gSavedContext.r10 = 0;
  gSavedContext.r11 = 0x293;
  gSavedContext.r12 = 0x7ffdef1adf40;
  gSavedContext.r13 = 0x5642b81a0e90;
  gSavedContext.r14 = 0x7ffdef1adfac;
  gSavedContext.r15 = 0;
  gSavedContext.ip = 0x7f6b0a481d26;
  gSavedContext.flags = 0x293;
  gSavedContext.cs = 0x33;
  gSavedContext.ss = 0x2b;

  asm("jmp RestoreRegisters");

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

