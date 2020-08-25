/*
 *  Migration Handler
 *
 */
#include "State.h"
#include "StateLayout.h"
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

static inline pudval_t pud_flags_mask(pud_t pud)
{
        return ~pud_pfn_mask(pud);
}

static inline pudval_t pud_flags(pud_t pud)
{
        return native_pud_val(pud) & pud_flags_mask(pud);
}

#define mk32 (((UINT64)1 << 32) - 1)
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

  //new_pte = __pte(pa & PTE_MASK); // TODO | page-flags
  //DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER: AddPageToMapping: va=%xllx pte_index=%x\n", va, pte_index(va));
  //CopyMem(pte + pte_index(va),&new_pte,sizeof(pte_t));

  // i think we can just use memcpy here
  // here we are setting an entry in the pmd,
  // the destination is an index into the pmd that corresponds 
  // with the virtual address 
  // the source is the entry specifying the physical addres 
  new_pmd = __pmd((pa & PMD_MASK) | pgprot_val(pmd_text_prot));
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER: AddPageToMapping: va=0x%llx pmd_index(va)=0x%x\n", va, pmd_index(va));
  CopyMem(pmd + pmd_index(va),&new_pmd,sizeof(pmd_t));

  // basically the same 
  // in the kernel, this line uses the __pa macro to find the 
  // the phyiscal address of the omd (this entry points to the 
  // next node in the tree). since OVMF is direct-mapped, 
  // i think we can just use the address of the pmd directly. 
  // have to do some suspicious casting of pmd. 
  new_pud = __pud((UINT64)pmd | pgprot_val(pgtable_prot));
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER: AddPageToMapping: va=0x%llx pud_index(va)=0x%x\n", va, pud_index(va));
  CopyMem(pud + pud_index(va),&new_pud,sizeof(pud_t));

  new_pgd = __pgd((UINT64)pud | pgprot_val(pgtable_prot));
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER: AddPageToMapping: va=0x%llx pgd_index(va)=0x%x\n", va, pgd_index(va));
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

  // Change to test TestTarget's VA:
  //DebugPrint(DEBUG_ERROR,"MH: Changing target RIP to TestTarget (FAKESTATE)\n");
  //SourceState->regs.ip = 0xffff88800080cd00;

  // Add 16 to RIP to skip zzzloop:
  DebugPrint(DEBUG_ERROR,"MH: Adding 16 to target RIP to skip zzzloop\n");
  SourceState->regs.ip += 0x10;

  struct pt_regs source_regs = SourceState->regs;
  DebugPrint(DEBUG_ERROR,"MH: Looking for RIP in source pgt\n");
  GetPa(cr3_to_pgt_pa(SourceState->cr3), source_regs.ip);

  //DebugPrint(DEBUG_ERROR,"MH: Looking for RSP in source pgt\n");
  //GetPa(cr3_to_pgt_pa(SourceState->cr3), source_regs.sp);

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

  //---------------------------
  // This commented-out section is an attempt to copy our code+data to pages
  // allocated by AllocatePages instead of pages reserved by Pcd.
  //
  // gBS is NULL, so instaed I use SystemTable->BootServices
  //DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER Before AllocatePages\n");
  //EFI_PHYSICAL_ADDRESS StartAddress = 0x0;
  //EFI_STATUS Status = SystemTable->BootServices->AllocatePages (AllocateAnyPages, EfiRuntimeServicesCode, 3, &StartAddress);
  ////EFI_STATUS Status = SystemTable->BootServices->AllocatePages (AllocateAnyPages, EfiRuntimeServicesData, 3, &StartAddress);
  ////EFI_STATUS Status = SystemTable->BootServices->AllocatePages (AllocateAnyPages, EfiPersistentMemory, 3, &StartAddress);
  //DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER After  AllocatePages Status = %d StartAddress = %llx\n", Status, StartAddress);
  //ASSERT_EFI_ERROR (Status);
  //gRelocatedRestoreStep2 = StartAddress;
  //
  //---------------------------

  gRelocatedRestoreRegisters = gRelocatedRestoreStep2 + PAGE_SIZE;
  gRelocatedRestoreRegistersData = gRelocatedRestoreRegisters + PAGE_SIZE;
  UINT64 gRelocatedRestoreRegistersDataStart = gRelocatedRestoreRegistersData + CPU_STATE_OFFSET_IN_PAGE; // Extra 8 bytes so the IRETQ frame is 16-bytes aligned

  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER New pages: gRelocatedRestoreStep2 = %lx\n", gRelocatedRestoreStep2);
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER New pages: gRelocatedRestoreRegisters = %lx\n", gRelocatedRestoreRegisters);
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER New pages: gRelocatedRestoreRegistersData = %lx\n", gRelocatedRestoreRegistersData);

  CopyMem((void *)gRelocatedRestoreStep2,RestoreStep2,PAGE_SIZE);
  CopyMem((void *)gRelocatedRestoreRegisters,RestoreRegisters,PAGE_SIZE);
  // i don't think we actaully need to copy this. SourceState is already 
  // on its own page. we should be able to just add that page to the 
  // intermediate pagetable and set gRelocatedRestoreRegistersData 
  // accordingly. just going to leave for now
  ZeroMem((void *)gRelocatedRestoreRegistersData, PAGE_SIZE);
  CopyMem((void *)gRelocatedRestoreRegistersDataStart,SourceState,sizeof(*SourceState));

  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER New pages: content of gRelocatedRestoreRegistersDataStart = %a\n", (char*)((void*)gRelocatedRestoreRegistersDataStart));

  GenerateIntermediatePageTables();

#if 0
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER pgd = \n");
  for (int i = 0; i < ENTRIES; i++) {
    DebugPrint(DEBUG_ERROR,"%llx ", pgd[i]);
  }
  DebugPrint(DEBUG_ERROR,"\n");
  DebugPrint(DEBUG_ERROR,"pgd[0x111]=%llx\n", pgd[0x111]);

  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER pud = \n");
  for (int i = 0; i < ENTRIES; i++) {
    DebugPrint(DEBUG_ERROR,"%llx ", pud[i]);
  }
  DebugPrint(DEBUG_ERROR,"\n");
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER pmd = \n");
  for (int i = 0; i < ENTRIES; i++) {
    DebugPrint(DEBUG_ERROR,"%llx ", pmd[i]);
  }
  DebugPrint(DEBUG_ERROR,"\n");
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER pte = \n");
  for (int i = 0; i < ENTRIES; i++) {
    DebugPrint(DEBUG_ERROR,"%llx ", pte[i]);
  }
  DebugPrint(DEBUG_ERROR,"\n");
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER target CR3 pgd = \n");
  UINT64* target_pgd = (void*)(SourceState->cr3);
  for (int i = 0; i < ENTRIES; i++) {
    DebugPrint(DEBUG_ERROR,"%llx ", target_pgd[i]);
  }
  DebugPrint(DEBUG_ERROR,"\n");
#endif

  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER   Temp PGD = 0x%lx\n", cr3_to_pgt_pa(pgd));
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER Target PGD = 0x%lx\n", cr3_to_pgt_pa(SourceState->cr3));

  GetPa(cr3_to_pgt_pa(pgd), gRelocatedRestoreStep2);
  GetPa(cr3_to_pgt_pa(pgd), gRelocatedRestoreRegisters);

  // Switch to the copy of the code in the target's address space
  gRelocatedRestoreRegisters = (unsigned long)__va(gRelocatedRestoreRegisters);
  // Modify the target's page table to make our page executable
  ClearPageNXFlag(cr3_to_pgt_pa(SourceState->cr3), gRelocatedRestoreRegisters);
  GetPa(cr3_to_pgt_pa(pgd), gRelocatedRestoreRegisters);
  GetPa(cr3_to_pgt_pa(SourceState->cr3), gRelocatedRestoreRegisters);

  //DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER GetPa of Source GDT\n");
  //GetPa(cr3_to_pgt_pa(SourceState->cr3), 0xfffffe0000001000ULL);
  //DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER GetPa of Source IDT\n");
  //GetPa(cr3_to_pgt_pa(SourceState->cr3), 0xfffffe0000000000ULL);

  // This can help with gdb:
  //
  volatile int wait = 0;
  while (wait) {
    __asm__ __volatile__("pause");
  }

  //UINT64 handler_addr = 0xffff88800080cd00;
  //UINT64 dummy_idt[2];
  //dummy_idt[0] = (handler_addr & 0xffffULL) | (0x0010ULL << 16) | (0x8e00ULL << 32) | (((handler_addr >> 16) & 0xffffULL) << 48);
  //dummy_idt[1] = handler_addr >> 32;
  //DebugPrint(DEBUG_ERROR,"dummy_idt[0] = %016llx\n", dummy_idt[0]);
  //DebugPrint(DEBUG_ERROR,"dummy_idt[1] = %016llx\n", dummy_idt[1]);

  SystemTable->ConOut->OutputString(SystemTable->ConOut, L"MigrationHandler: Calling RestoreStep1\r\n");
  DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER calling RestoreStep1\n");

#if 0
  // TODO this should be recorded in cpu_state and restored from there.
  //
  // GDT area (virtual linux addresses):
  //
  // fffffe0000001000: 0x0000000000000000 0x00cf9b000000ffff
  // fffffe0000001010: 0x00af9b000000ffff 0x00cf93000000ffff
  // fffffe0000001020: 0x00cffb000000ffff 0x00cff3000000ffff
  // fffffe0000001030: 0x00affb000000ffff 0x0000000000000000
  // fffffe0000001040: 0x00008b0030004087 0x00000000fffffe00
  // fffffe0000001050: 0x0000000000000000 0x0000000000000000
  // fffffe0000001060: 0x0000000000000000 0x0000000000000000
  // fffffe0000001070: 0x0000000000000000 0x0040f50000000000
  //
  // Fix Linux GDT entries overwritten by OVMF.
  //
  // This might actually write over some OVMF libary code, so do this as the
  // last thing before calling out to our assembly functions (RestoreStep1).
  UINT64* linux_gdt = (void*)0x3f80b000; // <-- this is the physical address but we're in identity mapping in OVMF
  linux_gdt[ 0] = 0x0000000000000000ULL;
  linux_gdt[ 1] = 0x00cf9b000000ffffULL;
  linux_gdt[ 2] = 0x00af9b000000ffffULL; // CS=0x0010 (SI=2 TI=0 RPL=0)
  linux_gdt[ 3] = 0x00cf93000000ffffULL; // SS=0x0018 (SI=3 TI=0 RPL=0)
  linux_gdt[ 4] = 0x00cffb000000ffffULL;
  linux_gdt[ 5] = 0x00cff3000000ffffULL;
  linux_gdt[ 6] = 0x00affb000000ffffULL;
  linux_gdt[ 7] = 0x0000000000000000ULL;
  linux_gdt[ 8] = 0x00008b0030004087ULL;
  linux_gdt[ 9] = 0x00000000fffffe00ULL;
  linux_gdt[10] = 0x0000000000000000ULL;
  linux_gdt[11] = 0x0000000000000000ULL;
  linux_gdt[12] = 0x0000000000000000ULL;
  linux_gdt[13] = 0x0000000000000000ULL;
  linux_gdt[14] = 0x0000000000000000ULL;
  linux_gdt[15] = 0x0040f50000000000ULL;

  UINT64* my_idt = (void*)0x311c000; // <-- this is the physical address but we're in identity mapping in OVMF
  for (int i = 0; i < 256; i++) {
    //if (i == 11 /* NP */ || i == 12 || i == 13 /* GP */ || i == 14 /* PF */ ) {
    if (0){
     //   (i >= 11 && i <= 17) {
        // fffffe0000000ff0: 0x90 0x0c 0x10 0x00 0x00 0x8e 0xc0 0x81
        //
        // 0x81c08e0000100c90
        //
        // fffffe0000000ff8: 0xff 0xff 0xff 0xff 0x00 0x00 0x00 0x00
        //
      my_idt[i * 2] = (handler_addr & 0xffffULL) | (0x0010ULL << 16) | (0x8e00ULL << 32) | (((handler_addr >> 16) & 0xffffULL) << 48);
      my_idt[i * 2 + 1] = handler_addr >> 32;

    } else {
      my_idt[i * 2] = 0;
      my_idt[i * 2 + 1] = 0;
    }
  }

  // first two entries from linux IDT
  my_idt[0] = 0x81c08e0000100870;
  my_idt[1] = 0x00000000ffffffff;
  // intr 0  handler 0xffffffff81c00870
  my_idt[2] = 0x81c08e0300100b40;
  my_idt[3] = 0x00000000ffffffff;
#endif

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

