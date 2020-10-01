/*
 *  Migration Handler
 *
 *  UEFI Application that resumes into a VM Snapshot
 *
 */
#include "State.h"
#include "StateLayout.h"
#include "MigrationHandler.h"


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

// This is a temporary workaround. See main for details
int ClearPageNXFlag(UINT64 pgd_base, unsigned long long va){
    pgd_t *pgd;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *ptep;
    //DebugPrint(DEBUG_ERROR,"MH: ClearPageNXFlag: VA 0x%llx in PGT at 0x%llx\n", va, pgd_base);

    pgd = pgd_offset_pgd((pgd_t*)pgd_base, va);
    if (pgd_none(*pgd)) {
        return -1;
    }

    pud = pud_offset(pgd, va);
    if (pud_none(*pud)) {
        return -2;
    }

    pmd = pmd_offset(pud, va);
    if (pmd_none(*pmd)) {
        return -3;
    }

    ptep = pte_offset_kernel(pmd, va);
    if (!ptep) {
        return -4;
    }
    ptep->pte &= ~_PAGE_NX;

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
  CopyMem(pmd + pmd_index(va),&new_pmd,sizeof(pmd_t));

  new_pud = __pud((UINT64)pmd | pgprot_val(pgtable_prot));
  CopyMem(pud + pud_index(va),&new_pud,sizeof(pud_t));

  new_pgd = __pgd((UINT64)pud | pgprot_val(pgtable_prot));
  CopyMem(pgd + pgd_index(va), &new_pgd, sizeof(pgd_t));

}

// setup a page table for stage 2 of the trampoline 
// that maps the code for both stage 2 and stage 3.
static void GenerateIntermediatePageTables(){
  // since OVMF has a direct mapping, VA = PA
  AddPageToMapping(gRelocatedResumeCpuStatePhase2,gRelocatedResumeCpuStatePhase2);
  AddPageToMapping(gRelocatedRestoreRegisters,gRelocatedRestoreRegisters);
  AddPageToMapping(gRelocatedCpuStateDataPage,gRelocatedCpuStateDataPage);

  // Map the same physical pages also with the virtual addresses that will
  // refer to these pages in the Linux kernel's page mapping (offset mapping):
  AddPageToMapping((unsigned long)__va(gRelocatedResumeCpuStatePhase2),gRelocatedResumeCpuStatePhase2);
  AddPageToMapping((unsigned long)__va(gRelocatedRestoreRegisters),gRelocatedRestoreRegisters);
  AddPageToMapping((unsigned long)__va(gRelocatedCpuStateDataPage),gRelocatedCpuStateDataPage);

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
  SystemTable->ConOut->OutputString(SystemTable->ConOut, L"MIGRATION HANDLER\r\n");

  // The cpu state from the source is passed in via FW_CFG and 
  // copied to this page.
  UINT64 state_page_base = PcdGet32(PcdSevMigrationStatePageBase); 
  struct cpu_state *SourceState = (void *) state_page_base;   

  // we can access parts of this page via the pt_regs struct
  // we mainly use this for testing
  // maybe remove some of the testing code here

  char *magicstr = SourceState->magic;
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER SourceState->magic = %a\n", magicstr);

  gMMUCR4Features = SourceState->cr4;

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
  gRelocatedResumeCpuStatePhase2 = PcdGet32(PcdSevMigrationPagesBase);
  gRelocatedRestoreRegisters = gRelocatedResumeCpuStatePhase2 + PAGE_SIZE;
  gRelocatedCpuStateDataPage = gRelocatedRestoreRegisters + PAGE_SIZE;

  UINT64 gRelocatedCpuStateStart = gRelocatedCpuStateDataPage + CPU_STATE_OFFSET_IN_PAGE; // Extra 8 bytes so the IRETQ frame is 16-bytes aligned

  CopyMem((void *)gRelocatedResumeCpuStatePhase2,ResumeCpuStatePhase2,PAGE_SIZE);
  CopyMem((void *)gRelocatedRestoreRegisters,RestoreRegisters,PAGE_SIZE);

  ZeroMem((void *)gRelocatedCpuStateDataPage, PAGE_SIZE);
  CopyMem((void *)gRelocatedCpuStateStart, SourceState, sizeof(*SourceState));

  // Now add the mappings for stages two and three. 
  GenerateIntermediatePageTables();

  // Switch to the copy of the code in the target's address space
  gRelocatedRestoreRegisters = (unsigned long)__va(gRelocatedRestoreRegisters);
  // The final page of the trampoline must be mapped in the
  // source page table. The source table will be the kernel's
  // offset map. Currently we just assume that a mostly arbitrary
  // location will be mapped. For that to work, we need to modify
  // the NX bit of the source page table. 
  ClearPageNXFlag(cr3_to_pgt_pa(SourceState->cr3), gRelocatedRestoreRegisters);


  SystemTable->ConOut->OutputString(SystemTable->ConOut, L"Starting Trampoline\r\n");
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER calling ResumeCpuStatePhase1\n");

  // start the trampoline
  ResumeCpuStatePhase1(); 

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

