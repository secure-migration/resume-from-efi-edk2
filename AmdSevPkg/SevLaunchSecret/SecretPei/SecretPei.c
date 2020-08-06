#include <PiPei.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/PcdLib.h>

EFI_STATUS
EFIAPI
InitializeSecretPei (
  IN       EFI_PEI_FILE_HANDLE  FileHandle,
  IN CONST EFI_PEI_SERVICES     **PeiServices
  )
{
  BuildMemoryAllocationHob (
    PcdGet32 (PcdSevLaunchSecretBase),
    PcdGet32 (PcdSevLaunchSecretSize),
    EfiBootServicesData);

  BuildMemoryAllocationHob (
    PcdGet32 (PcdSevMigrationMailboxBase),
    PcdGet32 (PcdSevMigrationMailboxSize),
    EfiRuntimeServicesData);

  //
  // This region supposedly should be of type EfiRuntimeServicesData, because
  // we want the kernel not to use this physical memory area. However, if it is
  // marked as Runtime, then the kernel doesn't include this are in its kernel
  // mapping (page offset), and this makes it harder to jump to our page with a
  // kernel-valid virtual address.
  //
  // For now we mark this as Boot services, which makes the kernel map these
  // pages at virtual address (_PAGE_OFFSET+phys_addr); hopefully the kernel
  // doesn't use these 12K for something important.
  //
  BuildMemoryAllocationHob (
    PcdGet32 (PcdSevMigrationPagesBase),
    PcdGet32 (PcdSevMigrationPagesSize),
    EfiBootServicesData /* <--- this is the hack */);

  BuildMemoryAllocationHob (
    PcdGet32 (PcdSevMigrationStatePageBase),
    PcdGet32 (PcdSevMigrationStatePageSize),
    EfiRuntimeServicesData);


  return EFI_SUCCESS;
}
