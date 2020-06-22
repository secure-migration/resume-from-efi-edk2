#include <PiPei.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>

#define SEV_SECRET_TABLE_GUID {0xadf956ad, 0xe98c, 0x484c, {0xae, 0x11, 0xb5, 0x1c, 0x7d, 0x33, 0x64, 0x47}}


struct {
 GUID guid;
 UINT32 base;
 UINT32 size;
} SecretTable = {
 SEV_SECRET_TABLE_GUID,
 FixedPcdGet32(PcdSevLaunchSecretBase),
 FixedPcdGet32(PcdSevLaunchSecretSize),
};

EFI_STATUS
EFIAPI
_ModuleEntryPoint () {
 return (unsigned long)&SecretTable;
}
