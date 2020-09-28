/*
 *  Migration Handler
 *
 *  UEFI Application that resumes into a VM Snapshot
 *
 */
#include "State.h"
#include "StateLayout.h"
#include "MigrationHandler.h"


// Defined in RestoreState.nasm
void RestoreRegisters(void);
void RestoreStep1(void);
void RestoreStep2(void);

// helpers for pagetables. borrowed from Linux

static inline pudval_t native_pud_val(pud_t pud)
{
	return pud.pud;
}

static inline pmdval_t native_pmd_val(pmd_t pmd)
{
	return pmd.pmd;
}

static inline pudval_t pud_pfn_mask(pud_t pud)
{
	if (native_pud_val(pud) & _PAGE_PSE)
		return PHYSICAL_PUD_PAGE_MASK;
	else
		return PTE_PFN_MASK;
}

static inline pudval_t pud_flags_mask(pud_t pud)
{
        return ~pud_pfn_mask(pud);
}

static inline pudval_t pud_flags(pud_t pud)
{
        return native_pud_val(pud) & pud_flags_mask(pud);
}

static inline pmdval_t pmd_pfn_mask(pmd_t pmd)
{
	if (native_pmd_val(pmd) & _PAGE_PSE)
		return PHYSICAL_PMD_PAGE_MASK;
	else
		return PTE_PFN_MASK;
}

static inline pmdval_t pmd_flags_mask(pmd_t pmd)
{
        return ~pmd_pfn_mask(pmd);
}

static inline pmdval_t pmd_flags(pmd_t pmd)
{
        return native_pmd_val(pmd) & pmd_flags_mask(pmd);
}

static inline unsigned long pgd_page_vaddr(pgd_t pgd)
{
    return pgd_val(pgd) & PTE_PFN_MASK;
}

static inline unsigned long pud_page_vaddr(pud_t pud)
{
	return pud_val(pud) & pud_pfn_mask(pud);
}

static inline unsigned long pmd_page_vaddr(pmd_t pmd)
{
	return pmd_val(pmd) & pmd_pfn_mask(pmd);
}

/* Find an entry in the third-level page table.. */
static inline pud_t *pud_offset(pgd_t *pgd, unsigned long address)
{
	return (pud_t *)pgd_page_vaddr(*pgd) + pud_index(address);
}

/* Find an entry in the second-level page table.. */
static inline pmd_t *pmd_offset(pud_t *pud, unsigned long address)
{
	return (pmd_t *)pud_page_vaddr(*pud) + pmd_index(address);
}

static inline pte_t *pte_offset_kernel(pmd_t *pmd, unsigned long address)
{
	return (pte_t *)pmd_page_vaddr(*pmd) + pte_index(address);
}

// a function for testing page tables. this will traverse 
// a given page table tree and resolve a va to a pa.
int GetPa(UINT64 pgd_base, unsigned long long va){
    pgd_t *pgd;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *ptep;
    DebugPrint(DEBUG_ERROR,"MH: Searching for VA 0x%llx in PGT at 0x%llx\n",
            va, pgd_base);

    pgd = pgd_offset_pgd((pgd_t*)pgd_base, va);
    DebugPrint(DEBUG_ERROR, "> MH entry address is: %p\n", (void *)pgd);
    DebugPrint(DEBUG_ERROR, "> pgd value: %llx\n", pgd->pgd);
    if (pgd_none(*pgd)) 
        return -1;

    pud = pud_offset(pgd, va);
    DebugPrint(DEBUG_ERROR, ">> pud entry address is: %p\n", (void *)pud);
    DebugPrint(DEBUG_ERROR, ">> pud value: %llx\n", pud_val(*pud));
    DebugPrint(DEBUG_ERROR, ">> pud flags: %llx\n", pud_flags(*pud));
    DebugPrint(DEBUG_ERROR, ">> pud flags & _PAGE_PSE: %llx\n", pud_flags(*pud) & _PAGE_PSE);
    if (pud_none(*pud))
        return -2;

    pmd = pmd_offset(pud, va);
    DebugPrint(DEBUG_ERROR, ">>> pmd entry address is: %p\n", (void *)pmd);
    DebugPrint(DEBUG_ERROR, ">>> pmd value: %llx\n", pmd_val(*pmd));
    DebugPrint(DEBUG_ERROR, ">>> pmd flags: %llx\n", pmd_flags(*pmd));
    DebugPrint(DEBUG_ERROR, ">>> pmd flags & _PAGE_PSE: %llx\n", pmd_flags(*pmd) & _PAGE_PSE);
    if (pmd_none(*pmd))
        return -3;


    DebugPrint(DEBUG_ERROR, ">>>> pte_index(va)=%llx\n", pte_index(va));
    ptep = pte_offset_kernel(pmd, va);
    DebugPrint(DEBUG_ERROR, ">>>> pte entry address is: %p\n", (void *)ptep);
    DebugPrint(DEBUG_ERROR, ">>>> pte value: %llx\n", pte_val(*ptep));
    if (!ptep)
        return -4;

    return 0;
}

// This is a temporary workaround. See main for details
int ClearPageNXFlag(UINT64 pgd_base, unsigned long long va){
    pgd_t *pgd;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *ptep;
    DebugPrint(DEBUG_ERROR,"MH: ClearPageNXFlag: VA 0x%llx in PGT at 0x%llx\n", va, pgd_base);

    pgd = pgd_offset_pgd((pgd_t*)pgd_base, va);
    if (pgd_none(*pgd)) {
        DebugPrint(DEBUG_ERROR, "ClearPageNXFlag quitting > pgd value: %llx\n", pgd->pgd);
        return -1;
    }

    pud = pud_offset(pgd, va);
    if (pud_none(*pud)) {
        DebugPrint(DEBUG_ERROR, "ClearPageNXFlag quitting > pud value: %llx\n", pud_val(*pud));
        return -2;
    }

    pmd = pmd_offset(pud, va);
    if (pmd_none(*pmd)) {
        DebugPrint(DEBUG_ERROR, "ClearPageNXFlag quitting > pmd value: %llx\n", pmd_val(*pmd));
        return -3;
    }

    ptep = pte_offset_kernel(pmd, va);
    if (!ptep) {
        DebugPrint(DEBUG_ERROR, "ClearPageNXFlag quitting > pte value: %llx\n", pte_val(*ptep));
        return -4;
    }
    DebugPrint(DEBUG_ERROR, "ClearPageNXFlag: pte entry address is: %p\n", (void *)ptep);
    DebugPrint(DEBUG_ERROR, "ClearPageNXFlag: pte value before: %llx\n", pte_val(*ptep));
    ptep->pte &= ~_PAGE_NX;
    DebugPrint(DEBUG_ERROR, "ClearPageNXFlag: pte value after: %llx\n", pte_val(*ptep));

    return 0;
}


// Add a page to a page table tree. This will create a three-level
// mapping. Mostly borrowed from Linux.
static void AddPageToMapping(unsigned long va, unsigned long pa){
  pgd_t new_pgd;
  pud_t new_pud;
  pmd_t new_pmd;

  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER: Mapping 0x%llx to 0x%llx \n", va, pa);
  pgprot_t pgtable_prot = __pgprot(_KERNPG_TABLE);
  pgprot_t pmd_text_prot = __pgprot(__PAGE_KERNEL_LARGE_EXEC);

  pgprot_val(pmd_text_prot) &= __default_kernel_pte_mask;
  pgprot_val(pgtable_prot)  &= __default_kernel_pte_mask;

  new_pmd = __pmd((pa & PMD_MASK) | pgprot_val(pmd_text_prot));
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER: AddPageToMapping: va=0x%llx pmd_index(va)=0x%x\n", va, pmd_index(va));
  CopyMem(pmd + pmd_index(va),&new_pmd,sizeof(pmd_t));

  new_pud = __pud((UINT64)pmd | pgprot_val(pgtable_prot));
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER: AddPageToMapping: va=0x%llx pud_index(va)=0x%x\n", va, pud_index(va));
  CopyMem(pud + pud_index(va),&new_pud,sizeof(pud_t));

  new_pgd = __pgd((UINT64)pud | pgprot_val(pgtable_prot));
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER: AddPageToMapping: va=0x%llx pgd_index(va)=0x%x\n", va, pgd_index(va));
  CopyMem(pgd + pgd_index(va), &new_pgd, sizeof(pgd_t));

}

// setup a page table for stage 2 of the trampoline 
// that maps the code for both stage 2 and stage 3.
static void GenerateIntermediatePageTables(){
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER PrepareMemory pgd = %p\n", pgd);
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER PrepareMemory pud = %p\n", pud);
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER PrepareMemory pmd = %p\n", pmd);
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER PrepareMemory pte = %p\n", pte);

  // since OVMF has a direct mapping, VA = PA
  AddPageToMapping(gRelocatedRestoreStep2,gRelocatedRestoreStep2);
  AddPageToMapping(gRelocatedRestoreRegisters,gRelocatedRestoreRegisters);
  AddPageToMapping(gRelocatedRestoreRegistersData,gRelocatedRestoreRegistersData);

  // Map the same physical pages also with the virtual addresses that will
  // refer to these pages in the Linux kernel's page mapping (offset mapping):
  AddPageToMapping((unsigned long)__va(gRelocatedRestoreStep2),gRelocatedRestoreStep2);
  AddPageToMapping((unsigned long)__va(gRelocatedRestoreRegisters),gRelocatedRestoreRegisters);
  AddPageToMapping((unsigned long)__va(gRelocatedRestoreRegistersData),gRelocatedRestoreRegistersData);

  gTempPGT = (UINT64)pgd;
}


// Migration Handler Main
EFI_STATUS
EFIAPI
MigrationHandlerMain(
  IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
  )
{
  // The cpu state from the source is passed in via FW_CFG and 
  // copied to this page.
  UINT64 state_page_base = PcdGet32(PcdSevMigrationStatePageBase); 
  struct cpu_state *SourceState = (void *) state_page_base;   

  // we can access parts of this page via the pt_regs struct
  // we mainly use this for testing
  // maybe remove some of the testing code here
  struct pt_regs source_regs = SourceState->regs;
  DebugPrint(DEBUG_ERROR,"MH: Looking for RIP in source pgt\n");
  GetPa(cr3_to_pgt_pa(SourceState->cr3), source_regs.ip);
  
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER Address of RestoreRegisters = %p\n", RestoreRegisters);
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER Address of RestoreRegistersData = %p\n", &RestoreRegistersData);

  char *magicstr = SourceState->magic;
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER SourceState->magic = %a\n", magicstr);
  gSavedCR3 = SourceState->cr3;

  gMMUCR4Features = SourceState->cr4;
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER current CR4 = 0x%lx gMMUCR4Features = 0x%lx\n", AsmReadCr4(), gMMUCR4Features);

  // We need to be somewhat careful about how we setup the 
  // pages for our trampoline.
  // The trampoline has three stages, which each have one page, 
  // plus an addiditional page that stores the CPU state from 
  // the source. 
  //
  // We can put the page for the first stage just about anywhere, 
  // so we won't move it. The pages for the second two stages 
  // need to be mapped in our intermediate page table. First, 
  // we will move them to a known address that we set aside 
  // with a HOB in PEI.
  gRelocatedRestoreStep2 = PcdGet32(PcdSevMigrationPagesBase);
  gRelocatedRestoreRegisters = gRelocatedRestoreStep2 + PAGE_SIZE;
  gRelocatedRestoreRegistersData = gRelocatedRestoreRegisters + PAGE_SIZE;

  UINT64 gRelocatedRestoreRegistersDataStart = gRelocatedRestoreRegistersData + CPU_STATE_OFFSET_IN_PAGE; // Extra 8 bytes so the IRETQ frame is 16-bytes aligned

  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER New pages: gRelocatedRestoreStep2 = %lx\n", gRelocatedRestoreStep2);
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER New pages: gRelocatedRestoreRegisters = %lx\n", gRelocatedRestoreRegisters);
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER New pages: gRelocatedRestoreRegistersData = %lx\n", gRelocatedRestoreRegistersData);

  CopyMem((void *)gRelocatedRestoreStep2,RestoreStep2,PAGE_SIZE);
  CopyMem((void *)gRelocatedRestoreRegisters,RestoreRegisters,PAGE_SIZE);
  
  ZeroMem((void *)gRelocatedRestoreRegistersData, PAGE_SIZE);
  CopyMem((void *)gRelocatedRestoreRegistersDataStart,SourceState,sizeof(*SourceState));

  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER New pages: content of gRelocatedRestoreRegistersDataStart = %a\n", (char*)((void*)gRelocatedRestoreRegistersDataStart));

  // Now add the mappings for stages two and three. 
  GenerateIntermediatePageTables();

  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER   Temp PGD = 0x%lx\n", cr3_to_pgt_pa(pgd));
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER Target PGD = 0x%lx\n", cr3_to_pgt_pa(SourceState->cr3));

  GetPa(cr3_to_pgt_pa(pgd), gRelocatedRestoreStep2);
  GetPa(cr3_to_pgt_pa(pgd), gRelocatedRestoreRegisters);

  // Switch to the copy of the code in the target's address space
  gRelocatedRestoreRegisters = (unsigned long)__va(gRelocatedRestoreRegisters);
  // The final page of the trampoline must be mapped in the
  // source page table. The source table will be the kernel's
  // offset map. Currently we just assume that a mostly arbitrary
  // location will be mapped. For that to work, we need to modify
  // the NX bit of the source page table. 
  ClearPageNXFlag(cr3_to_pgt_pa(SourceState->cr3), gRelocatedRestoreRegisters);
  GetPa(cr3_to_pgt_pa(pgd), gRelocatedRestoreRegisters);
  GetPa(cr3_to_pgt_pa(SourceState->cr3), gRelocatedRestoreRegisters);


  //SystemTable->ConOut->OutputString(SystemTable->ConOut, L"MigrationHandler: Calling RestoreStep1\r\n");
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER calling RestoreStep1\n");

  // start the trampoline
  RestoreStep1(); 

  return 0;
}


EFI_STATUS 
EFIAPI
_ModuleEntryPoint(
IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
  )
{
  return MigrationHandlerMain(ImageHandle, SystemTable);
}

