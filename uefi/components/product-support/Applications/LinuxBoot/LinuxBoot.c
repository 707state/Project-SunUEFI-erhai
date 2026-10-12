// SPDX-License-Identifier: BSD-2-Clause-Patent
// A standard boot option asks the resident core to load the real ESP image.
#include <Uefi.h>
#include <Library/PianoProductPumpLib.h>

EFI_STATUS EFIAPI PianoLinuxBootEntry(EFI_HANDLE Image, EFI_SYSTEM_TABLE *Table) {
  (VOID)Image;
  (VOID)Table;
  return PianoProductRequestNavigation(PIANO_PRODUCT_ACTION_REQUEST_BOOT_STABLE);
}
