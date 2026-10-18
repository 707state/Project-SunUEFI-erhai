// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Uefi.h>
#include <Library/DeviceBootManagerLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/DevicePathLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/FdtLib.h>
#include <Library/DevicePathLib.h>
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


// Read an entire file into a freshly allocated pool buffer.
STATIC EFI_STATUS ReadWholeFile (EFI_FILE_PROTOCOL *File, VOID **Buffer, UINTN *Bytes)
{
  EFI_FILE_INFO *Info = NULL;
  UINTN InfoSize = 0;
  EFI_STATUS Status;
  if (File == NULL || Buffer == NULL || Bytes == NULL) return EFI_INVALID_PARAMETER;
  *Buffer = NULL; *Bytes = 0;
  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, NULL);
  if (EFI_ERROR (Status) && Status != EFI_BUFFER_TOO_SMALL) return Status;
  if (InfoSize == 0) return EFI_UNSUPPORTED;
  Info = AllocatePool (InfoSize);
  if (Info == NULL) return EFI_OUT_OF_RESOURCES;
  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, Info);
  if (EFI_ERROR (Status)) { FreePool (Info); return Status; }
  UINTN Size = (UINTN)Info->FileSize;
  FreePool (Info);
  VOID *Buf = AllocatePool (Size);
  if (Buf == NULL) return EFI_OUT_OF_RESOURCES;
  Status = File->Read (File, &Size, Buf);
  if (EFI_ERROR (Status) || Size == 0) { FreePool (Buf); return EFI_ERROR(Status) ? Status : EFI_DEVICE_ERROR; }
  *Buffer = Buf; *Bytes = Size;
  return EFI_SUCCESS;
}
STATIC EFI_STATUS Root2Open (EFI_FILE_PROTOCOL *Root, EFI_FILE_PROTOCOL **File, CHAR16 *Name)
{
  if (Root == NULL || File == NULL) return EFI_INVALID_PARAMETER;
  return Root->Open (Root, File, Name, EFI_FILE_MODE_READ, 0);
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
    if (gST != NULL && gST->ConOut != NULL) Print (L"ESP scan: %u filesystems (%r)\r\n", (UINT32)FsCount, FsStatus);
    if (!EFI_ERROR (FsStatus) && FsHandles != NULL) {
      for (UINTN I = 0; I < FsCount; I++) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs = NULL;
        EFI_FILE_PROTOCOL *Root = NULL, *File = NULL;
        if (EFI_ERROR (gBS->HandleProtocol (FsHandles[I], &gEfiSimpleFileSystemProtocolGuid,
                                            (VOID **)&Fs)) || Fs == NULL ||
            EFI_ERROR (Fs->OpenVolume (Fs, &Root)) || Root == NULL) {
          if (gST != NULL && gST->ConOut != NULL) Print (L"  fs %u: open failed\r\n", (UINT32)(I + 1));
          continue;
        }
        EFI_STATUS OpenStatus = Root->Open (Root, &File, L"\\EFI\\BOOT\\BOOTAA64.EFI",
                                           EFI_FILE_MODE_READ, 0);
        if (EFI_ERROR (OpenStatus) || File == NULL) {
          if (gST != NULL && gST->ConOut != NULL) Print (L"  fs %u: no BOOTAA64 (%r)\r\n", (UINT32)(I + 1), OpenStatus);
          Root->Close (Root);
          continue;
        }
        // Read the whole kernel image.
        if (gST != NULL && gST->ConOut != NULL) Print (L"  fs %u: BOOTAA64 found, reading\r\n", (UINT32)(I + 1));
        EFI_FILE_INFO *Info = NULL;
        UINTN InfoSize = 0;
        EFI_STATUS InfoStatus = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, NULL);
        // First call with zero size legitimately returns EFI_BUFFER_TOO_SMALL and
        // fills InfoSize with the required buffer size — that is the query pattern,
        // not a failure. Only a missing/zero size after the call is fatal.
        if ((EFI_ERROR (InfoStatus) && InfoStatus != EFI_BUFFER_TOO_SMALL) || InfoSize == 0) {
          if (gST != NULL && gST->ConOut != NULL) Print (L"  fs %u: GetInfo(size) failed %r\r\n", (UINT32)(I + 1), InfoStatus);
          File->Close (File); Root->Close (Root); continue;
        }
        Info = AllocatePool (InfoSize);
        if (Info == NULL ||
            EFI_ERROR (InfoStatus = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSize, Info))) {
          if (gST != NULL && gST->ConOut != NULL) Print (L"  fs %u: GetInfo failed %r\r\n", (UINT32)(I + 1), InfoStatus);
          if (Info != NULL) FreePool (Info);
          File->Close (File); Root->Close (Root); continue;
        }
        UINTN Bytes = (UINTN)Info->FileSize;
        FreePool (Info);
        if (gST != NULL && gST->ConOut != NULL) Print (L"  fs %u: kernel is %lu bytes\r\n", (UINT32)(I + 1), (UINT64)Bytes);
        VOID *Kernel = AllocatePool (Bytes);
        if (Kernel == NULL) {
          if (gST != NULL && gST->ConOut != NULL) Print (L"  fs %u: alloc failed\r\n", (UINT32)(I + 1));
          File->Close (File); Root->Close (Root); continue;
        }
        if (EFI_ERROR (InfoStatus = File->Read (File, &Bytes, Kernel)) || Bytes == 0) {
          if (gST != NULL && gST->ConOut != NULL) Print (L"  fs %u: Read failed %r (%lu bytes)\r\n", (UINT32)(I + 1), InfoStatus, (UINT64)Bytes);
          FreePool (Kernel); File->Close (File); Root->Close (Root); continue;
        }
        File->Close (File);
        // Piano-style raw handoff (PianoRawLinuxBoot.c): copy kernel/dtb/initrd to
        // fixed reserved addresses, patch the FDT, ExitBootServices, disable the MMU
        // and jump to the raw arm64 entry (Image + text_offset). StartImage on the
        // EFI stub faulted on this firmware; the raw path is the proven one.
        #define RAW_KERNEL 0xA8000000ULL
        #define RAW_DTB    0xB0000000ULL
        #define RAW_INITRD 0xB0200000ULL
        // Read DTB and initramfs from the same volume (paths from kernel cmdline).
        EFI_FILE_PROTOCOL *DtbFile = NULL, *InitrdFile = NULL;
        VOID *DtbBuf = NULL, *InitrdBuf = NULL;
        UINTN DtbBytes = 0, InitrdBytes = 0;
        if (EFI_ERROR (Root2Open (Root, &DtbFile, L"\\EFI\\BOOT\\erhai.dtb")) ||
            EFI_ERROR (ReadWholeFile (DtbFile, &DtbBuf, &DtbBytes)) || DtbBuf == NULL) {
          if (gST != NULL && gST->ConOut != NULL) Print (L"  fs %u: DTB read failed\r\n", (UINT32)(I + 1));
        }
        if (EFI_ERROR (Root2Open (Root, &InitrdFile, L"\\EFI\\BOOT\\initramfs.cpio.gz")) ||
            EFI_ERROR (ReadWholeFile (InitrdFile, &InitrdBuf, &InitrdBytes)) || InitrdBuf == NULL) {
          if (gST != NULL && gST->ConOut != NULL) Print (L"  fs %u: initramfs read failed\r\n", (UINT32)(I + 1));
        }
        UINT64 TextOffset = ReadUnaligned64 ((CONST UINT64 *)((CONST UINT8 *)Kernel + 8));
        if (gST != NULL && gST->ConOut != NULL) {
          Print (L"  fs %u: kernel %lu B at +%lu, dtb %lu B, initrd %lu B\r\n",
                 (UINT32)(I + 1), (UINT64)Bytes, TextOffset, (UINT64)DtbBytes, (UINT64)InitrdBytes);
        }
        // Copy to the reserved raw addresses.
        CopyMem ((VOID *)(UINTN)RAW_KERNEL, Kernel, Bytes);
        if (DtbBuf != NULL && DtbBytes >= 40) {
          CopyMem ((VOID *)(UINTN)RAW_DTB, DtbBuf, DtbBytes);
        }
        if (InitrdBuf != NULL && InitrdBytes > 0) {
          CopyMem ((VOID *)(UINTN)RAW_INITRD, InitrdBuf, InitrdBytes);
        }
        // Patch the FDT: /chosen/bootargs, linux,initrd-start/end.
        {
          VOID *Fdt = (VOID *)(UINTN)RAW_DTB;
          INT32 Node = FdtPathOffset (Fdt, "/chosen");
          STATIC CONST CHAR8 Cmdline[] =
            "console=ttyMSM0,115200n8 loglevel=8 ignore_loglevel root=LABEL=sunuefi_root "
            "rootfstype=ext4 rootwait rw pd_ignore_unused clk_ignore_unused iommu.passthrough=1 panic=15";
          if (Node >= 0) {
            FdtSetProp (Fdt, Node, "bootargs", Cmdline, sizeof (Cmdline));
            if (InitrdBytes > 0) {
              UINT64 Start = RAW_INITRD, End = RAW_INITRD + InitrdBytes;
              FdtSetPropU64 (Fdt, Node, "linux,initrd-start", Start);
              FdtSetPropU64 (Fdt, Node, "linux,initrd-end", End);
            }
            // Memory reservation for the kernel itself is via /memory Reg in DTB.
          }
        }
        Root->Close (Root);
        if (gST != NULL && gST->ConOut != NULL) Print (L"Starting kernel (raw)...\r\n");
        // --- ExitBootServices + MMU-off + naked jump (piano RawEnter) ---
        {
          UINTN MapSize = 0, MapKey = 0, DescSize = 0; UINT32 DescVersion = 0;
          EFI_STATUS MStatus = gBS->GetMemoryMap (&MapSize, NULL, &MapKey, &DescSize, &DescVersion);
          if (MStatus == EFI_BUFFER_TOO_SMALL && MapSize > 0) {
            EFI_MEMORY_DESCRIPTOR *Map = AllocatePool (MapSize + 4 * DescSize);
            if (Map != NULL) {
              MStatus = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVersion);
              if (!EFI_ERROR (MStatus)) {
                MStatus = gBS->ExitBootServices (gImageHandle, MapKey);
              }
              // After ExitBootServices no BS calls. On failure fall through to reboot.
            }
          }
          if (gST != NULL && gST->ConOut != NULL) Print (L"EBS %r\r\n", MStatus);
        }
        {
          // Piano's RawEnter: DAIF masked, disable MMU caches, jump.
          UINT64 Tree = RAW_DTB;
          UINT64 Entry = RAW_KERNEL + TextOffset;
          __asm__ volatile (
            "mov x4, %[entry]\n"
            "msr daifset, #15\n"
            "msr cntv_ctl_el0, xzr\n"
            "mrs x9, sctlr_el1\n"
            "mov x10, #5\n"
            "bic x9, x9, x10\n"
            "dsb sy\n"
            "msr sctlr_el1, x9\n"
            "isb\n"
            "ic iallu\n"
            "dsb sy\n"
            "isb\n"
            "mov x1, xzr\n"
            "mov x2, xzr\n"
            "mov x3, xzr\n"
            "mov x0, %[tree]\n"
            "br x4\n"
            :: [entry] "r" (Entry), [tree] "r" (Tree) : "x0","x1","x2","x3","x4","x9","x10", "memory");
        }
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
