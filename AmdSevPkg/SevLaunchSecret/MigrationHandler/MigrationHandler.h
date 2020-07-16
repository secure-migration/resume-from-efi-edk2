#include <Library/UefiLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseLib.h> 
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>
#include <Library/MemoryAllocationLib.h>

#include <string.h>

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

UINT64 gRelocatedRestoreRegisters;
UINT64 gTempPGT;
UINT64 gMMUCR4Features;
UINT64 gRelocatedRestoreStep2;

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

// basic helpers 
// from include/uapi/linux/const.h
#define _AT(T,X)    ((T)(X))
#define __AC(X,Y)   (X##Y)
#define _AC(X,Y)    __AC(X,Y)

#define __pa(x)     __phys_addr((unsigned long)(x))

// basic types for pages
typedef unsigned long   pteval_t;

typedef unsigned long   pmdval_t;
typedef unsigned long   pudval_t;
typedef unsigned long   pteval_t;
typedef unsigned long   pgdval_t;

typedef struct { pgdval_t pmd; } pgd_t;
typedef struct { pudval_t pmd; } pud_t;
typedef struct { pmdval_t pmd; } pmd_t;
typedef struct { pmdval_t pmd; } pte_t;

#define __pgd(x)    ((pgd_t) { (x) } )
#define __pmd(x)    ((pmd_t) { (x) } )
#define __pud(x)    ((pud_t) { (x) } )
#define __pte(x)    ((pte_t) { (x) } )

// Each level of our page table tree has 512 entries of 64-bit each 
// (which together fit in one 4096-byte page)
#define ENTRIES (EFI_PAGE_SIZE / sizeof(UINT64))

// the four levels of our page table (in order)
extern UINT64 pgd[ENTRIES];
extern UINT64 pud[ENTRIES];
extern UINT64 pmd[ENTRIES];
extern UINT64 pte[ENTRIES];


// arch/x86/include/asm/pgtable_types.h
extern pteval_t __default_kernel_pte_mask;

// A bunch of page types from arch/x86/incude/asm/pgtable_types.h
// We probably don't need every single one.
#define _PAGE_BIT_PRESENT   0   /* is present */
#define _PAGE_BIT_RW        1   /* writeable */
#define _PAGE_BIT_USER      2   /* userspace addressable */
#define _PAGE_BIT_PWT       3   /* page write through */
#define _PAGE_BIT_PCD       4   /* page cache disabled */
#define _PAGE_BIT_ACCESSED  5   /* was accessed (raised by CPU) */
#define _PAGE_BIT_DIRTY     6   /* was written to (raised by CPU) */
#define _PAGE_BIT_PSE       7   /* 4 MB (or 2MB) page */
#define _PAGE_BIT_PAT       7   /* on 4KB pages */
#define _PAGE_BIT_GLOBAL    8   /* Global TLB entry PPro+ */
#define _PAGE_BIT_SOFTW1    9   /* available for programmer */
#define _PAGE_BIT_SOFTW2    10  /* " */
#define _PAGE_BIT_SOFTW3    11  /* " */
#define _PAGE_BIT_PAT_LARGE 12  /* On 2MB or 1GB pages */
#define _PAGE_BIT_SOFTW4    58  /* available for programmer */
#define _PAGE_BIT_PKEY_BIT0 59  /* Protection Keys, bit 1/4 */
#define _PAGE_BIT_PKEY_BIT1 60  /* Protection Keys, bit 2/4 */
#define _PAGE_BIT_PKEY_BIT2 61  /* Protection Keys, bit 3/4 */
#define _PAGE_BIT_PKEY_BIT3 62  /* Protection Keys, bit 4/4 */
#define _PAGE_BIT_NX        63  /* No execute: only valid after cpuid check */

// shift over the above 
#define _PAGE_PRESENT   (_AT(pteval_t, 1) << _PAGE_BIT_PRESENT)
#define _PAGE_RW    (_AT(pteval_t, 1) << _PAGE_BIT_RW)
#define _PAGE_USER  (_AT(pteval_t, 1) << _PAGE_BIT_USER)
#define _PAGE_PWT   (_AT(pteval_t, 1) << _PAGE_BIT_PWT)
#define _PAGE_PCD   (_AT(pteval_t, 1) << _PAGE_BIT_PCD)
#define _PAGE_ACCESSED  (_AT(pteval_t, 1) << _PAGE_BIT_ACCESSED)
#define _PAGE_DIRTY (_AT(pteval_t, 1) << _PAGE_BIT_DIRTY)
#define _PAGE_PSE   (_AT(pteval_t, 1) << _PAGE_BIT_PSE)
#define _PAGE_GLOBAL    (_AT(pteval_t, 1) << _PAGE_BIT_GLOBAL)
#define _PAGE_SOFTW1    (_AT(pteval_t, 1) << _PAGE_BIT_SOFTW1)
#define _PAGE_SOFTW2    (_AT(pteval_t, 1) << _PAGE_BIT_SOFTW2)
#define _PAGE_SOFTW3    (_AT(pteval_t, 1) << _PAGE_BIT_SOFTW3)
#define _PAGE_PAT   (_AT(pteval_t, 1) << _PAGE_BIT_PAT)
#define _PAGE_PAT_LARGE (_AT(pteval_t, 1) << _PAGE_BIT_PAT_LARGE)
#define _PAGE_SPECIAL   (_AT(pteval_t, 1) << _PAGE_BIT_SPECIAL)
#define _PAGE_CPA_TEST  (_AT(pteval_t, 1) << _PAGE_BIT_CPA_TEST)

// this is slightly odd
#define sme_me_mask  0ULL
#define _PAGE_ENC       (_AT(pteval_t, sme_me_mask))

// rename
#define __PP _PAGE_PRESENT
#define __RW _PAGE_RW
#define _USR _PAGE_USER
#define ___A _PAGE_ACCESSED
#define ___D _PAGE_DIRTY
#define ___G _PAGE_GLOBAL
#define __NX _PAGE_NX

#define _ENC _PAGE_ENC
#define __WP _PAGE_CACHE_WP
#define __NC _PAGE_NOCACHE
#define _PSE _PAGE_PSE

// more complex types
#define PAGE_NONE        __pg(   0|   0|   0|___A|   0|   0|   0|___G)
#define PAGE_SHARED      __pg(__PP|__RW|_USR|___A|__NX|   0|   0|   0)
#define PAGE_SHARED_EXEC     __pg(__PP|__RW|_USR|___A|   0|   0|   0|   0)
#define PAGE_COPY_NOEXEC     __pg(__PP|   0|_USR|___A|__NX|   0|   0|   0)
#define PAGE_COPY_EXEC       __pg(__PP|   0|_USR|___A|   0|   0|   0|   0)
#define PAGE_COPY        __pg(__PP|   0|_USR|___A|__NX|   0|   0|   0)
#define PAGE_READONLY        __pg(__PP|   0|_USR|___A|__NX|   0|   0|   0)
#define PAGE_READONLY_EXEC   __pg(__PP|   0|_USR|___A|   0|   0|   0|   0)

#define __PAGE_KERNEL        (__PP|__RW|   0|___A|__NX|___D|   0|___G)
#define __PAGE_KERNEL_EXEC   (__PP|__RW|   0|___A|   0|___D|   0|___G)
#define _KERNPG_TABLE_NOENC  (__PP|__RW|   0|___A|   0|___D|   0|   0)
#define _KERNPG_TABLE        (__PP|__RW|   0|___A|   0|___D|   0|   0| _ENC)
#define _PAGE_TABLE_NOENC    (__PP|__RW|_USR|___A|   0|___D|   0|   0)
#define _PAGE_TABLE      (__PP|__RW|_USR|___A|   0|___D|   0|   0| _ENC)
#define __PAGE_KERNEL_RO     (__PP|   0|   0|___A|__NX|___D|   0|___G)
#define __PAGE_KERNEL_RX     (__PP|   0|   0|___A|   0|___D|   0|___G)
#define __PAGE_KERNEL_NOCACHE    (__PP|__RW|   0|___A|__NX|___D|   0|___G| __NC)
#define __PAGE_KERNEL_VVAR   (__PP|   0|_USR|___A|__NX|___D|   0|___G)
#define __PAGE_KERNEL_LARGE  (__PP|__RW|   0|___A|__NX|___D|_PSE|___G)
#define __PAGE_KERNEL_LARGE_EXEC (__PP|__RW|   0|___A|   0|___D|_PSE|___G)
#define __PAGE_KERNEL_WP     (__PP|__RW|   0|___A|__NX|___D|   0|___G| __WP)

// The kernel does this weird thing for type checking 
// I don't entirely understand why or if we need both.
typedef struct { unsigned long pgprot; } pgprot_t;
//typedef unsigned long pgprot_t;
#define __pgprot(x)     ((pgprot_t) { (x) } )

#define pgprot_val(x)   ((x).pgprot)

// Masking and shifting

// hopefully 
#define PTE_ORDER 0
#define PGD_ORDER 0

#define PAGE_SHIFT 12 // 2^12 = 4096, I think this is what we want...


#define PMD_SHIFT   (PAGE_SHIFT + (PAGE_SHIFT + PTE_ORDER - 3))
#define PMD_SIZE    (_AC(1, UL) << PMD_SHIFT)
#define PMD_MASK    (~(PMD_SIZE - 1))
#define PMD_BITS    (PAGE_SHIFT - 3)

#define PUD_SHIFT   (PMD_SHIFT + PMD_BITS)
#define PUD_SIZE    (_AC(1, UL) << PUD_SHIFT)
#define PUD_MASK    (~(PUD_SIZE - 1))
#define PUD_BITS    (PAGE_SHIFT - 3)

#define PGDIR_SHIFT (PUD_SHIFT + PUD_BITS)
#define PGDIR_SIZE  (_AC(1,UL) << PGDIR_SHIFT)
#define PGDIR_MASK  (~(PGDIR_SIZE-1))
#define PGDIR_BITS  (PAGE_SHIFT - 3)

// need to find PMD SHIFT
#define PTRS_PER_PMD ENTRIES
#define PTRS_PER_PUD ENTRIES
#define PTRS_PER_PGD ENTRIES

#define pmd_index(address)  (((address) >> PMD_SHIFT) & (PTRS_PER_PMD - 1))
#define pgd_index(address)  (((address) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))
#define pud_index(x)    (((x) >> PUD_SHIFT) & (PTRS_PER_PUD-1))

#define PAGE_SIZE 4096
#define FIXMAP_PMD_NUM 2 // not totally sure about this

// not really sure about this
#define __default_kernel_pte_mask ~0


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
