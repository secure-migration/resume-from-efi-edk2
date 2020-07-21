/*
 *  Migration Handler
 *
 */
#include "MigrationHandler.h"

void MyTarget(void) {
  DEBUG((DEBUG_ERROR,"MIGRATION HANDLER - inside MyTarget 111\n"));
}

static void SetCPUState()
{
  gSavedContext.ax    = 0xffffffff9e1c0350;
  gSavedContext.bx    = 0x0;
  gSavedContext.cx    = 0x0;
  gSavedContext.dx    = 0x0;
  gSavedContext.si    = 0x0;
  gSavedContext.di    = 0x0;
  gSavedContext.bp    = 0xffffffff9ec03e28;
  gSavedContext.sp    = 0xffffffff9ec03e28;
  gSavedContext.r8    = 0x0000002bd57e3220;
  gSavedContext.r9    = 0xffff9e6b2c341a00;
  gSavedContext.r10   = 0x0;
  gSavedContext.r11   = 0x000000e518faa9fb;
  gSavedContext.r12   = 0x0;
  gSavedContext.r13   = 0x0;
  gSavedContext.r14   = 0x0;
  gSavedContext.r15   = 0x000000003be683c0;
  gSavedContext.flags = 0x00000246;
  gSavedContext.ip    = 0xffffffffb77c06b2; // linux top ?
  //gSavedContext.ip    = 0x7f6b0a481d26; // grub?
  //gSavedContext.ip    = (unsigned long)(MyTarget); // 0x7f6b0a481d26;
  gSavedContext.cs    = 0x10;
  gSavedContext.ss    = 0x18;

  gSavedRIP = gSavedContext.ip;
  gSavedCR0 = 0x80050033;
  gSavedCR2 = 0x00007f5c1ad91bf0;
  gSavedCR3 = 0x000000003b8a2000;
  gSavedCR4 = 0x003406f0;

  gSavedGDTDesc.address = 0xfffffe0000001000;
  gSavedGDTDesc.size = 0x0000007f;

  gMMUCR4Features = AsmReadCr4();
}

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
  memcpy(pmd + pmd_index(va),&new_pmd,sizeof(pmd_t));

  // basically the same 
  // in the kernel, this line uses the __pa macro to find the 
  // the phyiscal address of the omd (this entry points to the 
  // next node in the tree). since OVMF is direct-mapped, 
  // i think we can just use the address of the pmd directly. 
  // have to do some suspicious casting of pmd. 
  new_pud = __pud((UINT64)pmd | pgprot_val(pgtable_prot));
  memcpy(pud + pud_index(va),&new_pud,sizeof(pud_t));

  new_pgd = __pgd((UINT64)pud | pgprot_val(pgtable_prot));
  memcpy(pgd + pgd_index(va), &new_pgd, sizeof(pgd_t));

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
  volatile struct pt_regs *SourceState = (void *) state_page_base;   
  // avoiding unused error
  SourceState->ax = 3;

  // Trampoline code can live here temporarily.
  
  // populate our state structs
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER Address of SetCPUState = %p\n", SetCPUState);
  SetCPUState();
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER Before PcdGet64\n");
  UINT64 newCR3 = PcdGet64(PcdMigrationStateCR3);
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER After PcdGet64 newCR3 = %x\n", newCR3);
  gSavedCR3 = newCR3;
  DebugPrint(DEBUG_ERROR,"JKLJL MIGRATION HANDLER After SetCPUState gSavedCR3 = %x.\n",gSavedCR3);
 
  // do we actually need to relocate this? can't we just leave it where 
  // it is and just jump to the function in asm
  // at the moment i am not sure what the value of gRelocatedBlah is
  // do we need to point that to a Pcd??

  gRelocatedRestoreStep2 = PcdGet32(PcdSevMigrationMailboxBase); 
  gRelocatedRestoreRegisters = gRelocatedRestoreStep2 + PAGE_SIZE;

  memcpy((void *)gRelocatedRestoreStep2,RestoreStep2,PAGE_SIZE);
  memcpy((void *)gRelocatedRestoreRegisters,RestoreRegisters,PAGE_SIZE);

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

