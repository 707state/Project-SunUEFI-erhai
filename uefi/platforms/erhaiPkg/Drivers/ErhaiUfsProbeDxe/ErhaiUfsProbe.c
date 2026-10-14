// SPDX-License-Identifier: BSD-2-Clause-Patent
// erhai UFS controller/clock probe, modeled on PianoUfsProbe.c (piano's product
// never uses the vendor UFSDxe/SPMI/PmicDxe stack — it holds the UFS clocks via the
// captured ClockDxe protocol and probes the ABL-trained controller read-only).
#include <Uefi.h>
#include <Protocol/EFIClock.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/FdtLib.h>
#include <Library/IoLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <PiDxe.h>
#include <Library/DxeServicesLib.h>
#include <Protocol/FirmwareVolume2.h>

#define HCI 0x01D84000U
#define DTB_GUID \
  { 0x25462cda, 0x221f, 0x47df, { 0xac, 0x1d, 0x25, 0x9c, 0xfa, 0xa4, 0xe3, 0x26 } }

STATIC EFI_CLOCK_PROTOCOL *mHeldClock;
STATIC UINTN mHeldDomain, mHeldIds[10];
STATIC UINTN mHeldCount;
STATIC BOOLEAN mDomainHeld;

STATIC UINT32 Be32(CONST UINT8 *P) {
  return ((UINT32)P[0] << 24) | ((UINT32)P[1] << 16) | ((UINT32)P[2] << 8) | P[3];
}

STATIC BOOLEAN Controller(CONST VOID *Fdt) {
  STATIC CONST UINT32 Reg[] = {HCI, 0x3000, 0x01D88000, 0x18000,
                               0x01DA5000, 0x2000, 0x01DA4000, 0x10};
  INT32 N = FdtPathOffset(Fdt, "/soc/ufshc@1d84000"), Len;
  if (N < 0) return FALSE;
  CONST UINT8 *P = FdtGetProp(Fdt, N, "reg", &Len);
  if (P == NULL || Len != sizeof(Reg)) return FALSE;
  for (UINTN I = 0; I < ARRAY_SIZE(Reg); ++I)
    if (Be32(P + 4 * I) != Reg[I]) return FALSE;
  P = FdtGetProp(Fdt, N, "iommus", &Len);
  return P != NULL && Len == 12 && Be32(P + 4) == 0x60;
}

STATIC VOID Probe(CONST VOID *Fdt) {
  if (mHeldClock != NULL) {
    DEBUG((DEBUG_WARN, "SUNUEFI_UFS_CLOCK_ALREADY_HELD\n"));
    return;
  }
  if (!Controller(Fdt)) {
    DEBUG((DEBUG_WARN, "SUNUEFI_UFS_DT_REJECTED\n"));
    return;
  }
  EFI_CLOCK_PROTOCOL *Clock = NULL;
  EFI_STATUS Status = gBS->LocateProtocol(&gEfiClockProtocolGuid, NULL, (VOID **)&Clock);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN, "SUNUEFI_UFS_CLOCK_PROTOCOL %r\n", Status));
    return;
  }
  if (Clock->Version != 0x1000b ||
      (VOID *)Clock->GetClockPowerDomainID != (VOID *)Clock->GetClockID ||
      (VOID *)Clock->EnableClockPowerDomain != (VOID *)Clock->EnableClock ||
      (VOID *)Clock->DisableClockPowerDomain != (VOID *)Clock->DisableClock) {
    DEBUG((DEBUG_WARN, "SUNUEFI_UFS_CLOCK_ABI_REJECTED\n"));
    return;
  }
  STATIC CONST CHAR8 *Names[] = {
    "gcc_ufs_phy_ahb_clk", "gcc_aggre_ufs_phy_axi_clk", "gcc_ufs_phy_axi_clk",
    "gcc_ufs_phy_unipro_core_clk", "gcc_ufs_phy_ice_core_clk", "gcc_ufs_phy_phy_aux_clk",
    "gcc_ufs_phy_rx_symbol_0_clk", "gcc_ufs_phy_rx_symbol_1_clk",
    "gcc_ufs_phy_tx_symbol_0_clk", "tcsr_ufs_clkref_en"
  };
  UINTN Domain = 0;
  Status = Clock->GetClockPowerDomainID(Clock, "gcc_ufs_phy_gdsc", &Domain);
  if (!EFI_ERROR(Status)) Status = Clock->EnableClockPowerDomain(Clock, Domain);
  DEBUG((DEBUG_WARN, "SUNUEFI_UFS_DOMAIN %r\n", Status));
  if (EFI_ERROR(Status)) return;
  mHeldClock = Clock;
  mHeldDomain = Domain;
  mDomainHeld = TRUE;
  mHeldCount = 0;
  for (UINTN I = 0; I < ARRAY_SIZE(Names); ++I) {
    Status = Clock->GetClockID(Clock, Names[I], &mHeldIds[I]);
    if (!EFI_ERROR(Status)) Status = Clock->EnableClock(Clock, mHeldIds[I]);
    DEBUG((DEBUG_WARN, "SUNUEFI_UFS_CLOCK %a %r\n", Names[I], Status));
    if (EFI_ERROR(Status)) goto Cleanup;
    mHeldCount++;
  }
  UINT32 Cap = MmioRead32(HCI);
  DEBUG((DEBUG_WARN, "SUNUEFI_UFS_REG cap=%08x version=%08x\n", Cap, MmioRead32(HCI + 8)));
  DEBUG((DEBUG_WARN, "SUNUEFI_UFS_PROBE_HELD clocks=%u\n", (UINT32)mHeldCount));
  return;
Cleanup:
  while (mHeldCount) {
    mHeldCount--;
    Clock->DisableClock(Clock, mHeldIds[mHeldCount]);
  }
  if (mDomainHeld) Clock->DisableClockPowerDomain(Clock, mHeldDomain);
  mDomainHeld = FALSE;
  mHeldClock = NULL;
}

EFI_STATUS EFIAPI ErhaiUfsProbeEntry(
  IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{
  // Load the platform DTB from the FV (FREEFORM 25462CDA, staged by the build) —
  // the same source DtPlatformDxe uses.
  STATIC EFI_GUID Guid = DTB_GUID;
  VOID *Dtb = NULL;
  UINTN DtbSize = 0;
  EFI_STATUS Status = GetSectionFromAnyFv(&Guid, EFI_SECTION_RAW, 0, &Dtb, &DtbSize);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_WARN, "SUNUEFI_UFS_DTB_FV %r\n", Status));
    return Status;
  }
  Probe((CONST VOID *)Dtb);
  FreePool(Dtb);
  return EFI_SUCCESS;
}
