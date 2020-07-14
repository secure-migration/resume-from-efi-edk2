#include <Library/UefiLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseLib.h> 
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/MemoryAllocationLib.h>



/* 
 * TARGET STATE 
 *
 * For now, we store the target state in OVMF using 
 * the following structures.
 *
 * pr_regs is from Linux kernel: arch/x86/include/asm/ptrace.h
 */
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


/*
 * CREATING INTERMEDIATE PAGE TABLES
 *
 * The following are needed to create the intermediate 
 * page table used by the second stage of our trampoline.
 */


// Each level of our page table tree has 512 entries of 64-bit each 
// (which together fit in one 4096-byte page)
#define ENTRIES (EFI_PAGE_SIZE / sizeof(UINT64))

// the four levels of our page table (in order)
extern UINT64 pgd[ENTRIES];
extern UINT64 pud[ENTRIES];
extern UINT64 pmd[ENTRIES];
extern UINT64 pte[ENTRIES];


/*
 * MHm <--> VMM Communication
 *
 * The the sev_mh_params struct defines the layout 
 * of the mailbox that the VMM and MHm will use
 * to communicate.
 */
struct sev_mh_params {
    unsigned long nr;
    unsigned long gpa;
    int do_prefetch;
    int ret; 
    int go;
    int done;
};

/* 
 * These are the commands that the MHm 
 * can execute. (Values of NR in above struct)
 */
#define FUNC_INIT 0
#define FUNC_SAVE_PAGE 1
#define FUNC_RESTORE_PAGE 2
#define FUNC_EXIT 3
#define FUNC_START 4

/*
 * Return codes (ret in params struct) 
 */
#define INVALID_FUNC (-1)
#define AUTH_ERR (-2)
