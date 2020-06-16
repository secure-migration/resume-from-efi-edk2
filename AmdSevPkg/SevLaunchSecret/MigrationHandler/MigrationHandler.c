#include <Library/UefiLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseLib.h> 
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PrintLib.h>

// This won't build
// #include <stdio.h>

EFI_STATUS
EFIAPI
MigrationHandlerMain(
  IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
  )
{
     
  // for now just do something we can see
  while(1) {
    // this doesn't seem to do anything? 
    // maybe prints to a log somewhere
    DEBUG((DEBUG_ERROR,"MIGRATION HANDLER\n")); 
    DebugPrint(DEBUG_ERROR,"MIGRATION HANDLER\n"); 
    
    // this requires stdio (see above)
    //printf("MIGRATION HANDLER\n");
    
    // This causes a nice panic.
    //Print(L"MIGRATION HANDLER\n"); 
  }
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
