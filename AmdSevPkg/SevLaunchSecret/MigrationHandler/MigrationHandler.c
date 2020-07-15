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

// Defined in RestoreState.nasm
void RestoreRegisters(void);

// setup a page table for stage 2 of the trampoline 
// that maps the code for both stage 2 and stage 3.
static void GenerateIntermediatePageTables(){
  // this should be the address of restore_registers
  // in the image kernel/ovmf
  // since OVMF has a direct mapping, these should be the same?
  unsigned long restore_jump_address = 0xC0000;
  unsigned long jump_address_phys = restore_jump_address;

  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER PrepareMemory pgd = %p\n", pgd);
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER PrepareMemory pud = %p\n", pud);
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER PrepareMemory pmd = %p\n", pmd);
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER PrepareMemory pte = %p\n", pte);

  // not sure
  pgprot_t pgtable_prot = __pgprot(_KERNPG_TABLE);
  pgprot_t pmd_text_prot = __pgprot(__PAGE_KERNEL_LARGE_EXEC);

  /* Filter out unsupported __PAGE_KERNEL* bits: */
  // look into this more
  /* pgprot_val(pmd_text_prot) &= __default_kernel_pte_mask; */
  /* pgprot_val(pgtable_prot)  &= __default_kernel_pte_mask; */

  // i think we can just use memcpy here
  // here we are setting an entry in the pmd,
  // the destination is an index into the pmd that corresponds 
  // with the virtual address 
  // the source is the entry specifying the physical addres 
  // The last argument should either be sizeof(pmd_t) or sizeof(pmdval)
  // I
  memcpy(pmd + pmd_index(restore_jump_address),&__pmd((jump_address_phys & PMD_MASK) | pgprot_val(pmd_text_prot)),sizeof(pmdval_t));

  // basically the same 
  // in the kernel, this line uses the __pa macro to find the 
  // the phyiscal address of the omd (this entry points to the 
  // next node in the tree). since OVMF is direct-mapped, 
  // i think we can just use the address of the pmd directly. 
  // have to do some suspicious casting of pmd. 
  memcpy(pud + pud_index(restore_jump_address),&__pud((UINT64)pmd | pgprot_val(pgtable_prot)),sizeof(pudval_t));

  pgd_t new_pgd = __pgd((UINT64)pud | pgprot_val(pgtable_prot));
  memcpy(pgd + pgd_index(restore_jump_address), &new_pgd, sizeof(pgdval_t));

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
  
  // Trampoline code can live here temporarily.
  
  // populate our state structs
  SetCPUState();
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER After SetCPUState 222.8 gSavedRIP = %016lx\n", gSavedRIP);
  
  // we might want another function before this puts pages in the 
  // correct location
  GenerateIntermediatePageTables();

  // we don't jump here directly anymore. for now this is a place
  // holder for our jump to stage 2
  RestoreRegisters();

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

