// SPDX-License-Identifier: BSD-2-Clause-Patent
// erhai UFS read-only BlockIO driver: piano's DMA engine ported as-is.
// The SM8750 HCI (0x01D84000), apps SMMU (0x15000000) and UFS stream ID 0x60 are
// identical between piano and erhai; the only platform input is the device tree,
// loaded from the FV FREEFORM staged by the build.
#include <Uefi.h>
#include <PiDxe.h>
#include <Protocol/FirmwareVolume2.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/DxeServicesLib.h>
#include <Library/FdtLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>

EFI_STATUS PianoUfsReadOnlyDmaExperiment (CONST VOID *Fdt);

#define DTB_GUID \
  { 0x25462cda, 0x221f, 0x47df, { 0xac, 0x1d, 0x25, 0x9c, 0xfa, 0xa4, 0xe3, 0x26 } }

EFI_STATUS EFIAPI ErhaiUfsDxeEntry (
  IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  STATIC EFI_GUID Guid = DTB_GUID;
  VOID *Dtb = NULL;
  UINTN DtbSize = 0;
  EFI_STATUS Status = GetSectionFromAnyFv(&Guid, EFI_SECTION_RAW, 0, &Dtb, &DtbSize);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN, "ERHAI_UFS_DTB_FV %r\n", Status));
    return Status;
  }
  // GetSectionFromAnyFv may return an FV-internal pointer; copy to pool memory.
  VOID *DtbCopy = AllocatePool(DtbSize);
  if (DtbCopy == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  CopyMem(DtbCopy, Dtb, DtbSize);

  DEBUG((DEBUG_WARN, "ERHAI_UFS_START dtb_bytes=%lu\n", (UINT64)DtbSize));
  // The piano engine holds the UFS clocks, opens the owned SMMU domain for
  // stream 0x60, retrains the inherited link and publishes read-only BlockIO
  // for every reportable LUN.
  Status = PianoUfsReadOnlyDmaExperiment((CONST VOID *)DtbCopy);
  DEBUG((DEBUG_WARN, "ERHAI_UFS_RESULT %r\n", Status));
  // Keep the DTB copy for this driver's lifetime: the SMMU context stores the
  // FDT pointer and re-captures snapshots on close paths.
  return Status;
}
