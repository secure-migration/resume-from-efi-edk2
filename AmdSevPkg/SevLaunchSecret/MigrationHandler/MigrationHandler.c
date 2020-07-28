/*
 *  Migration Handler
 *
 */
#include "State.h"
#include "MigrationHandler.h"


// Defined in RestoreState.nasm
void RestoreRegisters(void);
void RestoreStep1(void);
void RestoreStep2(void);

// helpers for pagetable walk

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

#define mk32 (((UINT64)1 << 32) - 1)
static inline pmdval_t pmd_pfn_mask(pmd_t pmd)
{
	if (native_pmd_val(pmd) & _PAGE_PSE)
		return PHYSICAL_PMD_PAGE_MASK;
	else
		return PTE_PFN_MASK;
}


static inline unsigned long pgd_page_vaddr(pgd_t pgd)
{
	//return (unsigned long)__va((unsigned long)pgd_val(pgd) & PTE_PFN_MASK & mk32);
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


int GetPa(UINT64 pgd_base, unsigned long long va){
    pgd_t *pgd;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *ptep;
    DebugPrint(DEBUG_ERROR,"MH: Searching for VA 0x%llx in PGT at 0x%llx\n",
            va, pgd_base);

    pgd = (pgd_t *)pgd_offset_pgd(pgd_base, va);
    DebugPrint(DEBUG_ERROR, "> MH entry address is: %p\n", (void *)pgd);
    DebugPrint(DEBUG_ERROR, "> pgd value: %llx\n", *pgd);
    if (pgd_none(*pgd)) 
        return -1;

    pud = pud_offset(pgd, va);
    DebugPrint(DEBUG_ERROR, ">> pud entry address is: %p\n", (void *)pud);
    DebugPrint(DEBUG_ERROR, ">> pud value: %llx\n", pud_val(*pud));
    if (pud_none(*pud))
        return -2;

    pmd = pmd_offset(pud, va);
    DebugPrint(DEBUG_ERROR, ">>> pmd entry address is: %p\n", (void *)pmd);
    DebugPrint(DEBUG_ERROR, ">>> pmd value: %llx\n",*pmd);
    if (pmd_none(*pmd))
        return -3;

    ptep = pte_offset_kernel(pmd, va);
    DebugPrint(DEBUG_ERROR, ">>>> pte entry address is: %p\n", (void *)ptep);
    DebugPrint(DEBUG_ERROR, ">>>> pte value: %llx\n",*ptep);
    if (!ptep)
        return -4;

    return 0;
}

// this seems a bit too simple. for one thing, are we actually doing 
// all four levels? 
// do we also need to mark the page as ex?
static void AddPageToMapping(unsigned long va, unsigned long pa){
  pgd_t new_pgd;
  pud_t new_pud;
  pmd_t new_pmd;
  /* pte_t new_pte; */

  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER: Mapping 0x%llx to 0x%llx \n", va, pa);
  pgprot_t pgtable_prot = __pgprot(_KERNPG_TABLE);
  pgprot_t pmd_text_prot = __pgprot(__PAGE_KERNEL_LARGE_EXEC);

  /* Filter out unsupported __PAGE_KERNEL* bits: */
  // look into this more
  // does nothing with our current setup
  pgprot_val(pmd_text_prot) &= __default_kernel_pte_mask;
  pgprot_val(pgtable_prot)  &= __default_kernel_pte_mask;

  // i think we can just use memcpy here
  // here we are setting an entry in the pmd,
  // the destination is an index into the pmd that corresponds 
  // with the virtual address 
  // the source is the entry specifying the physical addres 
  new_pmd = __pmd((pa & PMD_MASK) | pgprot_val(pmd_text_prot));
  CopyMem(pmd + pmd_index(va),&new_pmd,sizeof(pmd_t));

  // basically the same 
  // in the kernel, this line uses the __pa macro to find the 
  // the phyiscal address of the omd (this entry points to the 
  // next node in the tree). since OVMF is direct-mapped, 
  // i think we can just use the address of the pmd directly. 
  // have to do some suspicious casting of pmd. 
  new_pud = __pud((UINT64)pmd | pgprot_val(pgtable_prot));
  CopyMem(pud + pud_index(va),&new_pud,sizeof(pud_t));

  new_pgd = __pgd((UINT64)pud | pgprot_val(pgtable_prot));
  CopyMem(pgd + pgd_index(va), &new_pgd, sizeof(pgd_t));

  // i think we need something with the pte as well
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
  // Setup the mailbox
  UINT64 params_base = PcdGet32(PcdSevMigrationMailboxBase); 
  volatile struct sev_mh_params *params = (void *) params_base;   

  UINT64 state_page_base = PcdGet32(PcdSevMigrationStatePageBase); 
  struct cpu_state *SourceState = (void *) state_page_base;   

  struct pt_regs source_regs = SourceState->regs;
  DebugPrint(DEBUG_ERROR,"MH: Looking for RIP in source pgt\n");
  GetPa(cr3_to_pgt_pa(SourceState->cr3), source_regs.ip);

  DebugPrint(DEBUG_ERROR,"MH: Looking for RSP in source pgt\n");
  GetPa(cr3_to_pgt_pa(SourceState->cr3), source_regs.sp);

  // Trampoline code can live here temporarily.
  
  // populate our state structs
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER Address of RestoreRegisters = %p\n", RestoreRegisters);
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER Address of RestoreRegistersData = %p\n", &RestoreRegistersData);

  char *magicstr = SourceState->magic;
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER SourceState->magic = %a\n", magicstr);
  gSavedCR3 = SourceState->cr3;

  gMMUCR4Features = SourceState->cr4;
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER current CR4 = 0x%lx gMMUCR4Features = 0x%lx\n", AsmReadCr4(), gMMUCR4Features);

  // relocate pages
  gRelocatedRestoreStep2 = PcdGet32(PcdSevMigrationPagesBase);
  gRelocatedRestoreRegisters = gRelocatedRestoreStep2 + PAGE_SIZE;
  gRelocatedRestoreRegistersData = gRelocatedRestoreRegisters + PAGE_SIZE;

  CopyMem((void *)gRelocatedRestoreStep2,RestoreStep2,PAGE_SIZE);
  CopyMem((void *)gRelocatedRestoreRegisters,RestoreRegisters,PAGE_SIZE);
  // i don't think we actaully need to copy this. SourceState is already 
  // on its own page. we should be able to just add that page to the 
  // intermediate pagetable and set gRelocatedRestoreRegistersData 
  // accordingly. just going to leave for now
  CopyMem((void *)gRelocatedRestoreRegistersData,SourceState,sizeof(*SourceState));
  // Also try to copy the state data into the last 1KB of the page, in case we
  // find out we can only use one page during RestoreRegisters. (0xC00 = 3KB)
  CopyMem((void *)(gRelocatedRestoreRegisters + 0xc00),SourceState,sizeof(*SourceState));

  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER New pages: gRelocatedRestoreStep2 = %lx\n", gRelocatedRestoreStep2);
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER New pages: gRelocatedRestoreRegisters = %lx\n", gRelocatedRestoreRegisters);
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER New pages: gRelocatedRestoreRegistersData = %lx\n", gRelocatedRestoreRegistersData);
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER New pages: content of gRelocatedRestoreRegistersData = %a\n", (char*)((void*)gRelocatedRestoreRegistersData));

  GenerateIntermediatePageTables();

  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER   Temp PGD = 0x%lx\n", cr3_to_pgt_pa(pgd));
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER Target PGD = 0x%lx\n", cr3_to_pgt_pa(SourceState->cr3));

  GetPa(cr3_to_pgt_pa(pgd), gRelocatedRestoreStep2);
  GetPa(cr3_to_pgt_pa(pgd), gRelocatedRestoreRegisters);

  // Switch to the copy of the code in the target's address space
  gRelocatedRestoreRegisters = (unsigned long)__va(gRelocatedRestoreRegisters);
  GetPa(cr3_to_pgt_pa(pgd), gRelocatedRestoreRegisters);
  GetPa(cr3_to_pgt_pa(SourceState->cr3), gRelocatedRestoreRegisters);

  RestoreStep1(); 

  return 0;

  // Eventually we will use this loop to check the mailbox 
  // and encrypt/decrypt pages or trampoline on command.
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

