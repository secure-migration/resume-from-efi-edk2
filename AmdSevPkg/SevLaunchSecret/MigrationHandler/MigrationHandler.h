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
 *
 * This section will be fairly large. There are a handful 
 * of magic numbers below, the plan is to spend some time 
 * verifying once we at least have some idea where to find 
 * everything. 
 *
 * I should have written down where I found each of these things :|
 */

// A bunch of page types.
#define _PAGE_WT    0x001  /* CB0: if cacheable, 1->write-thru, 0->write-back */
#define _PAGE_DEVICE    0x001  /* CB0: if uncacheable, 1->device (i.e. no write-combining or reordering at bus level) */
#define _PAGE_CACHABLE  0x002  /* CB1: uncachable/cachable */
#define _PAGE_PRESENT   0x004  /* software: page referenced */
#define _PAGE_SIZE0 0x008  /* SZ0-bit : size of page */
#define _PAGE_SIZE1 0x010  /* SZ1-bit : size of page */
#define _PAGE_SHARED    0x020  /* software: reflects PTEH's SH */
#define _PAGE_READ  0x040  /* PR0-bit : read access allowed */
#define _PAGE_EXECUTE   0x080  /* PR1-bit : execute access allowed */
#define _PAGE_WRITE 0x100  /* PR2-bit : write access allowed */
#define _PAGE_USER  0x200  /* PR3-bit : user space access allowed */
#define _PAGE_DIRTY 0x400  /* software: page accessed in write */
#define _PAGE_ACCESSED  0x800  /* software: page referenced */

// some more complex types built from the ones above
#define _KERNPG_TABLE	(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | \
			 _PAGE_EXECUTE | \
			 _PAGE_CACHABLE | _PAGE_ACCESSED | _PAGE_DIRTY | \
			 _PAGE_SHARED)

#define __PAGE_KERNEL_EXEC						\
	(_PAGE_PRESENT | _PAGE_RW | _PAGE_DIRTY | _PAGE_ACCESSED | _PAGE_GLOBAL)

#define _PAGE_BIT_PSE	7	
#define _PAGE_PSE	(_AT(pteval_t, 1) << _PAGE_BIT_PSE)

#define __PAGE_KERNEL_LARGE_EXEC	(__PAGE_KERNEL_EXEC | _PAGE_PSE)

// simple macros for casting each type of page table
#define __pgt(x)    ((pgt_t) { (x) } )
#define __pmd(x)    ((pmd_t) { (x) } )
#define __pud(x)    ((pud_t) { (x) } )
#define __pte(x)    ((pte_t) { (x) } )

// Each level of our page table tree has 512 entries of 64-bit each 
// (which together fit in one 4096-byte page)
#define ENTRIES (EFI_PAGE_SIZE / sizeof(UINT64))

// The kernel does this weird thing for type checking 
// I don't entirely understand why or if we need both.
typedef struct { unsigned long pgprot; } pgprot_t;
typedef unsigned long pgprot_t;

#define pgprot_val(x)   ((x).pgprot)

// hopefully 
#define PTE_ORDER 0
#define PGD_ORDER 0

# define PAGE_SHIFT 12 // 2^12 = 4096, I think this is what we want...
#define PMD_SHIFT   (PAGE_SHIFT + (PAGE_SHIFT + PTE_ORDER - 3))
#define PMD_MASK    (~(PMD_SIZE-1))
// need to find PMD SHIFT
#define PTRS_PER_PMD ENTRIES
#define pmd_index(address)  (((address) >> PMD_SHIFT) & (PTRS_PER_PMD - 1))

#define PAGE_SIZE 4096
#define FIXMAP_PMD_NUM 2 // not totally sure about this

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
