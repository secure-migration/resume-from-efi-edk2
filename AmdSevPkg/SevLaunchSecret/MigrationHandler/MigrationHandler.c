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

// this seems a bit too simple. for one thing, are we actually doing 
// all four levels? 
// do we also need to mark the page as ex?
static void AddPageToMapping(unsigned long va, unsigned long pa){
  pgd_t new_pgd;
  pud_t new_pud;
  pmd_t new_pmd;
  /* pte_t new_pte; */

  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER: Mapping 0x%x to 0x%x \n", va, pa);
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

  // Trampoline code can live here temporarily.
  
  // populate our state structs
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER Address of RestoreRegisters = %p\n", RestoreRegisters);
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER Address of RestoreRegistersData = %p\n", &RestoreRegistersData);

  char *magicstr = SourceState->magic;
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER SourceState->magic = %a\n", magicstr);
  gSavedCR3 = SourceState->cr3;

  // I am slightly hazy about how w should be handling cr4
  gMMUCR4Features = AsmReadCr4();
  // which one? 
  //gMMUCR4Features = SourceState->cr4;
  // dubek: the second one doesn't work for me.
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER SourceState->cr4 = 0x%lx gMMUCR4Features = 0x%lx\n", SourceState->cr4, gMMUCR4Features);

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

