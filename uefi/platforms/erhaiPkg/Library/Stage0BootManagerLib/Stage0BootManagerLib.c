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
#include <Protocol/LoadedImage.h>
#include <Guid/FileInfo.h>
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
  if (gST != NULL && gST->ConOut != NULL) Print (L"Storage stack active. Attempting ESP kernel launch.\r\n");
  {
    // Direct boot of the ESP kernel (EFI stub): enumerate SimpleFileSystem, look for
    // \EFI\BOOT\BOOTAA64.EFI, LoadImage + StartImage. This is the erhai
    // equivalent of piano's controlled handoff — done at the designed BDS moment
    // after all drivers are connected.
    EFI_HANDLE *FsHandles = NULL;
    UINTN FsCount = 0;
    EFI_STATUS FsStatus = gBS->LocateHandleBuffer (ByProtocol, &gEfiSimpleFileSystemProtocolGuid,
                                                    NULL, &FsCount, &FsHandles);
    if (!EFI_ERROR (FsStatus) && FsHandles != NULL) {
      for (UINTN I = 0; I < FsCount; I++) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
        EFI_FILE_PROTOCOL *Root = NULL, *File = NULL;
        if (EFI_ERROR (gBS->HandleProtocol (FsHandles[I], &gEfiSimpleFileSystemProtocolGuid,
                                            (VOID **)&Fs)) || Fs == NULL ||
            EFI_ERROR (Fs->OpenVolume (Fs, &Root)) || Root == NULL) {
          continue;
        }
        EFI_STATUS OpenStatus = Root->Open (Root, &File, L"\\EFI\\BOOT\\BOOTAA64.EFI",
                                           EFI_FILE_MODE_READ, 0);
        if (EFI_ERROR (OpenStatus) || File == NULL) {
          Root->Close (Root);
          continue;
        }
        // Read the whole kernel image.
        EFI_FILE_INFO *Info = NULL;
        UINTN InfoSize = 0;
        if (EFI_ERROR (File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, NULL)) ||
            InfoSize == 0) { File->Close (File); Root->Close (Root); continue; }
        Info = AllocatePool (InfoSize);
        if (Info == NULL ||
            EFI_ERROR (File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, Info))) {
          if (Info != NULL) FreePool (Info);
          File->Close (File); Root->Close (Root); continue;
        }
        UINTN Bytes = (UINTN)Info->FileSize;
        FreePool (Info);
        VOID *Kernel = AllocatePool (Bytes);
        if (Kernel == NULL) { File->Close (File); Root->Close (Root); continue; }
        if (EFI_ERROR (File->Read (File, &Bytes, Kernel)) || Bytes == 0) {
          FreePool (Kernel); File->Close (File); Root->Close (Root); continue;
        }
        File->Close (File);
        Root->Close (Root);
        EFI_HANDLE Image = NULL;
        EFI_STATUS LoadStatus = gBS->LoadImage (FALSE, FsHandles[I], NULL, Kernel, (UINTN)Bytes, &Image);
        if (gST != NULL && gST->ConOut != NULL) {
          Print (L"ESP kernel: %lu bytes, LoadImage %r\r\n", (UINT64)Bytes, LoadStatus);
        }
        if (EFI_ERROR (LoadStatus)) {
          FreePool (Kernel);
          continue;
        }
        // Set the kernel command line via LoadedImage device path convention:
        // the EFI stub picks up cmdline from the optional data of the load option.
        // Simplest supported path: set it via gRT->SetVariable(L"KernelCmdline"...)
        // is nonstandard; instead the stub reads it from the FDT /chosen/bootargs
        // which our DTB provides. Boot now.
        if (gST != NULL && gST->ConOut != NULL) Print (L"Starting kernel...\r\n");
        EFI_STATUS StartStatus = gBS->StartImage (Image, NULL, NULL);
        if (gST != NULL && gST->ConOut != NULL) {
          Print (L"StartImage returned %r\r\n", StartStatus);
        }
        FreePool (Kernel);
        break;
      }
      gBS->FreePool (FsHandles);
    }
  }
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
