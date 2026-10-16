// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/DeviceBootManagerLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Library/UefiBootManagerLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/DevicePath.h>
#include <Library/MemoryAllocationLib.h>

STATIC EFI_EVENT mReturnToAndroidEvent;

STATIC VOID EFIAPI ReturnToAndroid (IN EFI_EVENT Event, IN VOID *Context)
{
  DEBUG ((DEBUG_WARN, "ERHAI_STAGE0_RETURN_TO_ANDROID\n"));
  gRT->ResetSystem (EfiResetCold, EFI_SUCCESS, 0, NULL);
}

EFI_HANDLE EFIAPI DeviceBootManagerBeforeConsole (
  OUT EFI_DEVICE_PATH_PROTOCOL **DevicePath,
  OUT BDS_CONSOLE_CONNECT_ENTRY **PlatformConsoles)
{
  if (DevicePath != NULL) { *DevicePath = NULL; }
  if (PlatformConsoles != NULL) { *PlatformConsoles = NULL; }
  return NULL;
}

EFI_DEVICE_PATH_PROTOCOL **EFIAPI DeviceBootManagerAfterConsole (VOID)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;
  EFI_STATUS Status;
  UINT64 CurrentEl;

  __asm__ volatile ("mrs %0, CurrentEL" : "=r" (CurrentEl));
  // Standard BDS-time driver binding: connect every handle now that all drivers
  // (DiskIo/Partition/Fat and the UFS engine's BlockIO) are dispatched. This is
  // the designed moment for Partition->FAT->SimpleFileSystem binding.
  EfiBootManagerConnectAll ();
  DEBUG ((DEBUG_WARN, "ERHAI_STAGE0_CONNECT_ALL_DONE\n"));
  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  // Framebuffer-serial diagnostics: no console dependency, survives ConOut==NULL.
  DEBUG ((DEBUG_WARN, "ERHAI_STAGE0_AFTER_CONSOLE EL=%u GOP=%r\n", (UINT32)(CurrentEl >> 2), Status));
  {
    UINTN HandleCount = 0;
    EFI_HANDLE *Handles = NULL;
    EFI_STATUS SubStatus = gBS->LocateHandleBuffer (ByProtocol, &gEfiGraphicsOutputProtocolGuid,
                                                    NULL, &HandleCount, &Handles);
    DEBUG ((DEBUG_WARN, "ERHAI_STAGE0_GOP_HANDLES count=%u status=%r\n", (UINT32)HandleCount, SubStatus));
    if (!EFI_ERROR (SubStatus) && Handles != NULL) {
      gBS->FreePool (Handles);
    }
  }
  if (gST != NULL && gST->ConOut != NULL) {
    if (gST != NULL && gST->ConOut != NULL) Print (L"\r\nSunUEFI: OnePlus Pad 2 Pro / erhai / SM8750-AB\r\n");
  } else {
    DEBUG ((DEBUG_WARN, "ERHAI_STAGE0_CONOUT_NULL\n"));
  }
  {
    EFI_STATUS SubStatus;
    UINTN HandleCount = 0;
    EFI_HANDLE *Handles = NULL;
    UINTN Index;
    UINTN BlockCount = 0;
    UINTN FsCount = 0;
    SubStatus = gBS->LocateHandleBuffer (ByProtocol, &gEfiBlockIoProtocolGuid,
                                         NULL, &HandleCount, &Handles);
    if (!EFI_ERROR (SubStatus) && Handles != NULL) {
      for (Index = 0; Index < HandleCount; Index++) {
        EFI_BLOCK_IO_PROTOCOL *Block = NULL;
        if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiBlockIoProtocolGuid,
                                            (VOID **)&Block)) || Block == NULL) {
          continue;
        }
        BlockCount++;
        if (BlockCount <= 8 && Block->Media != NULL) {
          if (gST != NULL && gST->ConOut != NULL) Print (L"BlockIO %u: %llu MiB, block %u B, removable %u%s\r\n",
                 (UINT32)BlockCount,
                 (unsigned long long)((Block->Media->LastBlock + 1) *
                                      (UINT64)Block->Media->BlockSize) >> 20,
                 Block->Media->BlockSize,
                 Block->Media->RemovableMedia,
                 Block->Media->ReadOnly ? L" [RO]" : L"");
        }
      }
      gBS->FreePool (Handles);
    } else {
      if (gST != NULL && gST->ConOut != NULL) Print (L"BlockIO: none found (%r)\r\n", SubStatus);
    }
    SubStatus = gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid,
                                         NULL, &HandleCount, &Handles);
    if (!EFI_ERROR (SubStatus) && Handles != NULL) {
      for (Index = 0; Index < HandleCount; Index++) {
        FsCount++;
        if (FsCount <= 8) {
          EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
          EFI_FILE_PROTOCOL *Root = NULL;
          if (!EFI_ERROR (gBS->HandleProtocol (Handles[Index],
                                                &gEfiSimpleFileSystemProtocolGuid,
                                                (VOID **)&Fs)) && Fs != NULL &&
              !EFI_ERROR (Fs->OpenVolume (Fs, &Root)) && Root != NULL) {
            if (gST != NULL && gST->ConOut != NULL) Print (L"FileSystem %u opened\r\n", (UINT32)FsCount);
            Root->Close (Root);
          } else {
            if (gST != NULL && gST->ConOut != NULL) Print (L"FileSystem %u open failed\r\n", (UINT32)FsCount);
          }
        }
      }
      gBS->FreePool (Handles);
    } else {
      if (gST != NULL && gST->ConOut != NULL) Print (L"SimpleFileSystem: none found (%r)\r\n", SubStatus);
    }
    if (gST != NULL && gST->ConOut != NULL) Print (L"Storage summary: %u block devices, %u filesystems\r\n",
           (UINT32)BlockCount, (UINT32)FsCount);
  }
  if (gST != NULL && gST->ConOut != NULL) Print (L"Stage 0 diagnostic: DXE and console reached, EL%u\r\n", (UINT32)(CurrentEl >> 2));
  DEBUG ((DEBUG_WARN, "ERHAI_STAGE0_STORAGE_ENUM_DONE\n"));
  if (gST != NULL && gST->ConOut != NULL) Print (L"Internal storage and USB mass-storage drivers are excluded.\r\n");
  if (gST != NULL && gST->ConOut != NULL) Print (L"Linux and Windows PE boot are not implemented in this image.\r\n");
  if (!EFI_ERROR (Status) && Gop != NULL && Gop->Mode != NULL && Gop->Mode->Info != NULL) {
    if (gST != NULL && gST->ConOut != NULL) Print (L"GOP %ux%u, stride %u, framebuffer 0x%lx\r\n",
      Gop->Mode->Info->HorizontalResolution, Gop->Mode->Info->VerticalResolution,
      Gop->Mode->Info->PixelsPerScanLine, Gop->Mode->FrameBufferBase);
  }
  // This cannot recover a hang that happens before the console callback.
  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK,
                           ReturnToAndroid, NULL, &mReturnToAndroidEvent);
  if (!EFI_ERROR (Status)) {
    Status = gBS->SetTimer (mReturnToAndroidEvent, TimerRelative, 45ULL * 10000000ULL);
    if (!EFI_ERROR (Status)) {
      if (gST != NULL && gST->ConOut != NULL) Print (L"Automatic cold reboot requested in 45 seconds.\r\n");
    } else {
      gBS->CloseEvent (mReturnToAndroidEvent);
      mReturnToAndroidEvent = NULL;
    }
  }
  DEBUG ((DEBUG_WARN, "ERHAI_STAGE0_CONSOLE_READY EL%u\n", (UINT32)(CurrentEl >> 2)));
  return NULL;
}

EFI_DEVICE_PATH_PROTOCOL **EFIAPI DeviceBootManagerOnDemandConInConnect (VOID) { return NULL; }
VOID EFIAPI DeviceBootManagerProcessBootCompletion (OUT EFI_BOOT_MANAGER_LOAD_OPTION *BootOption) { }
EFI_STATUS EFIAPI DeviceBootManagerPriorityBoot (IN OUT EFI_BOOT_MANAGER_LOAD_OPTION *BootOption) { return EFI_NOT_FOUND; }
VOID EFIAPI DeviceBootManagerUnableToBoot (VOID) { }
VOID EFIAPI DeviceBootManagerBdsEntry (VOID) { }
