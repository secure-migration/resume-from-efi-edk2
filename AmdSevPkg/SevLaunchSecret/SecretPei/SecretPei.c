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

  BuildMemoryAllocationHob (
    PcdGet32 (PcdSevMigrationPagesBase),
    PcdGet32 (PcdSevMigrationPagesSize),
    EfiRuntimeServicesData);

  return EFI_SUCCESS;
}
