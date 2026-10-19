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
#include <Protocol/WatchdogTimer.h>
#include <Protocol/Timer.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Guid/FileInfo.h>
#include <Protocol/DevicePath.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/MemoryMapLib.h>

// Piano's proven raw layout: the 256 MiB "Kernel" reserved region.
#define RAW_KERNEL 0xA8000000ULL
#define RAW_DTB    0xB0000000ULL
#define RAW_INITRD 0xB0200000ULL
#define RAW_DTB_MAX 0x200000ULL

// Piano's Destination(): the target must be inside the reserved "Kernel"
// region described by MemoryMapLib, with the exact piano attributes. This is
// the same region pianoRawLinuxBoot writes without allocating it.
STATIC BOOLEAN RawDestination (UINT64 Address, UINT64 Bytes)
{
  EFI_MEMORY_REGION_DESCRIPTOR *Map = NULL;
  UINT8 Count = 0;

  if (Bytes == 0 || Address > MAX_UINT64 - Bytes) { return FALSE; }
  GetMemoryMap (&Map, &Count);
  if (Map == NULL) { return FALSE; }
  for (UINTN I = 0; I < Count; ++I) {
    if (!AsciiStrCmp (Map[I].Name, "Kernel") &&
        Map[I].Address == RAW_KERNEL && Map[I].Length == 0x10000000 &&
        Map[I].MemoryType == EfiReservedMemoryType &&
        Map[I].ResourceType == EFI_RESOURCE_SYSTEM_MEMORY &&
        Map[I].ArmAttributes == WRITE_BACK_XN &&
        Address >= Map[I].Address && Address - Map[I].Address < Map[I].Length &&
        Bytes <= Map[I].Length - (Address - Map[I].Address)) {
      return TRUE;
    }
  }
  return FALSE;
}

STATIC BOOLEAN RawOverlap (UINT64 A, UINT64 N, UINT64 B, UINT64 M)
{
  return A > MAX_UINT64 - N || B > MAX_UINT64 - M || (A < B + M && B < A + N);
}

// The erhai UFS driver installs the piano owner protocol; the piano product
// calls Halt before ExitBootServices so the OS gets a quiesced controller and
// a retained SMMU instead of a live DMA engine.
#define ERHAI_UFS_SHUTDOWN_GUID {0x3D43C751,0xC239,0x42D5,{0x95,0x9C,0x75,0x21,0xF0,0x18,0x02,0x60}}
typedef struct { UINT64 Revision; EFI_STATUS (*Halt) (VOID); } ERHAI_UFS_SHUTDOWN;

// 0 isolates the raw jump from the firmware ExitBootServices callback set.
// Set to 1 only after the callbacks are known to halt devices cleanly.
#ifndef ERHAI_RAW_EBS
#define ERHAI_RAW_EBS 0
#endif

// Diagnostic isolation: the UEFI leaves the UFS controller/SMMU active. The
// kernel's ufs-qcom probe runs before the initramfs and can hard-hang the SoC
// (watchdog -> QUSB_BULK EDL). Disable the UFS nodes so the RAM initramfs and
// its USB ACM console can come up; the rootfs mount needs UFS and is tested
// separately once the early boot path is known-good.
#ifndef ERHAI_DISABLE_UFS
#define ERHAI_DISABLE_UFS 0
#endif

// The UEFI's TZ/SCM drivers (TzDxeLA/ScmDxeCompat) use the same SMC conduit as
// the kernel's qcom_scm probe. Isolate that probe: with the SCM node disabled
// the kernel makes no TZ calls; USB/PHY/initramfs do not require SCM.
#ifndef ERHAI_DISABLE_SCM
#define ERHAI_DISABLE_SCM 0
#endif

// Diagnostic isolation: the UEFI console is framebuffer-only, so the GENI
// UART clock/pinctrl may never have been enabled. A kernel probe of an
// unclocked GENI block can hang the interconnect (watchdog -> QUSB_BULK EDL).
#ifndef ERHAI_DISABLE_UART
#define ERHAI_DISABLE_UART 0
#endif

STATIC EFI_EVENT mReturnToAndroidEvent;

// Enter the raw arm64 kernel: x0 = FDT, x4 = entry, DAIF masked, MMU and
// D-cache off. This must stay a standalone noinline function so its address
// can be reported (and so the compiler cannot reorder firmware code into the
// MMU-off window).
STATIC VOID __attribute__((naked, noinline, noreturn)) RawEnter (UINT64 Tree, UINT64 Entry)
{
  __asm__ volatile (
    "mov x5, x0\n"
    "mov x4, x1\n"
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
    "mov x0, x5\n"
    "br x4\n");
}

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
  {
    EFI_LOADED_IMAGE_PROTOCOL *Self = NULL;
    EFI_STATUS S = gBS->HandleProtocol (gImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Self);
    if (!EFI_ERROR (S) && Self != NULL) {
      if (gST != NULL && gST->ConOut != NULL) Print (L"Stage 0 image: base %lx size %lx entry %lx\r\n",
        (UINT64)(UINTN)Self->ImageBase, (UINT64)Self->ImageSize, (UINT64)(UINTN)RawEnter);
    }
  }
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
        // Piano-style raw handoff, adapted for this stage-0 image:
        //  - Never write to the fixed "Kernel" reserved region (0xA8000000). It is
        //    EfiReservedMemoryType in the EFI memory map, so AllocateAddress returns
        //    EFI_NOT_FOUND, and a direct CopyMem into it faulted on hardware
        //    (synchronous exception inside this driver's copy loop). Instead place
        //    the kernel in pages this driver allocates itself: the same kind of
        //    DXE-managed memory the 52 MiB file read above already proved writable.
        //  - The arm64 boot protocol (Documentation/arch/arm64/booting.rst) wants the
        //    Image at a 2 MiB-aligned base + text_offset, entry at the image start
        //    (code0 is the MZ "ccmp" pseudo-nop, code1 branches to primary_entry),
        //    with image_size bytes available (covers .bss beyond the file).
        //  - The vendor DTB ships /memory reg with size=0 (the Android bootloader
        //    fills it at boot time). Raw boot must write the real RAM range or the
        //    kernel sees no memory at all. Low bank: 0x80000000..0x100000000.
        #define ARM64_MAGIC_OFFSET 56
        #define LOW_MEM_BASE 0x80000000ULL
        #define LOW_MEM_SIZE 0x80000000ULL
        // Read DTB and initramfs from the same volume (paths from kernel cmdline).
        EFI_FILE_PROTOCOL *DtbFile = NULL, *InitrdFile = NULL;
        VOID *DtbBuf = NULL, *InitrdBuf = NULL;
        UINTN DtbBytes = 0, InitrdBytes = 0;
        if (EFI_ERROR (Root2Open (Root, &DtbFile, L"\\EFI\\BOOT\\erhai.dtb")) ||
            EFI_ERROR (ReadWholeFile (DtbFile, &DtbBuf, &DtbBytes)) || DtbBuf == NULL) {
          if (gST != NULL && gST->ConOut != NULL) Print (L"  fs %u: DTB read failed\r\n", (UINT32)(I + 1));
        }
        if (DtbFile != NULL) { DtbFile->Close (DtbFile); DtbFile = NULL; }
        if (EFI_ERROR (Root2Open (Root, &InitrdFile, L"\\EFI\\BOOT\\initramfs.cpio.gz")) ||
            EFI_ERROR (ReadWholeFile (InitrdFile, &InitrdBuf, &InitrdBytes)) || InitrdBuf == NULL) {
          if (gST != NULL && gST->ConOut != NULL) Print (L"  fs %u: initramfs read failed\r\n", (UINT32)(I + 1));
        }
        if (InitrdFile != NULL) { InitrdFile->Close (InitrdFile); InitrdFile = NULL; }
        // A raw jump without a valid DTB is a guaranteed crash: require it.
        if (DtbBuf == NULL || DtbBytes < 40 || FdtCheckHeader (DtbBuf) != 0 || DtbBytes > 0x200000) {
          if (gST != NULL && gST->ConOut != NULL) Print (L"  fs %u: DTB missing or invalid, skipping\r\n", (UINT32)(I + 1));
          if (DtbBuf != NULL) { FreePool (DtbBuf); DtbBuf = NULL; }
          FreePool (Kernel); Root->Close (Root); continue;
        }
        // Validate the arm64 Image header: magic "ARM\x64" at +56, text_offset and
        // image_size at +8/+16. The 52 MiB BOOTAA64.EFI is an EFI-stub Image; its
        // text_offset is 0, so the entry is the image start itself.
        UINT64 TextOffset = ReadUnaligned64 ((CONST UINT64 *)((CONST UINT8 *)Kernel + 8));
        UINT64 ImageSize = ReadUnaligned64 ((CONST UINT64 *)((CONST UINT8 *)Kernel + 16));
        UINT32 Magic = ReadUnaligned32 ((CONST UINT32 *)((CONST UINT8 *)Kernel + ARM64_MAGIC_OFFSET));
        if (Bytes < 64 || Magic != 0x644D5241 || TextOffset > 0x100000 || (TextOffset & 0xFFF) != 0) {
          if (gST != NULL && gST->ConOut != NULL) {
            Print (L"  fs %u: not an arm64 Image (magic %08x, +%lu)\r\n", (UINT32)(I + 1), Magic, TextOffset);
          }
          FreePool (DtbBuf); FreePool (Kernel); Root->Close (Root); continue;
        }
        if (ImageSize < Bytes) { ImageSize = Bytes; }
        UINTN Span = (UINTN)ImageSize;
        if (gST != NULL && gST->ConOut != NULL) {
          Print (L"  fs %u: kernel %lu B (span %lu at +%lu), dtb %lu B, initrd %lu B\r\n",
                 (UINT32)(I + 1), (UINT64)Bytes, ImageSize, TextOffset, (UINT64)DtbBytes, (UINT64)InitrdBytes);
        }
        // Piano raw placement: the 256 MiB reserved "Kernel" region at
        // 0xA8000000 (kernel), 0xB0000000 (DTB), 0xB0200000 (initrd). Piano
        // writes these fixed addresses without allocating them; the same
        // MemoryMapLib entry exists on erhai. Destination() rejects any address
        // outside that reserved region.
        UINTN TreeCapacity = DtbBytes + 0x10000;
        UINT64 KBase = RAW_KERNEL + TextOffset;
        if (TreeCapacity > RAW_DTB_MAX) {
          if (gST != NULL && gST->ConOut != NULL) Print (L"  dtb exceeds raw slot\r\n");
          FreePool (DtbBuf); FreePool (Kernel); Root->Close (Root); continue;
        }
        if (!RawDestination (KBase, Span) || !RawDestination (RAW_DTB, TreeCapacity) ||
            (InitrdBuf != NULL && InitrdBytes > 0 && !RawDestination (RAW_INITRD, InitrdBytes)) ||
            RawOverlap (KBase, Span, RAW_DTB, TreeCapacity) ||
            (InitrdBuf != NULL && InitrdBytes > 0 &&
             (RawOverlap (KBase, Span, RAW_INITRD, InitrdBytes) ||
              RawOverlap (RAW_DTB, TreeCapacity, RAW_INITRD, InitrdBytes)))) {
          if (gST != NULL && gST->ConOut != NULL) {
            Print (L"  raw placement rejected: kernel %lx+%lx dtb %lx+%lx initrd %lx+%lx\r\n",
                   KBase, (UINT64)Span, (UINT64)RAW_DTB, (UINT64)TreeCapacity,
                   (UINT64)RAW_INITRD, (UINT64)InitrdBytes);
          }
          FreePool (DtbBuf); FreePool (Kernel); Root->Close (Root); continue;
        }
        // Piano order: copy the file image, then zero the .bss tail of the span.
        CopyMem ((VOID *)(UINTN)KBase, Kernel, Bytes);
        if (Span > Bytes) {
          ZeroMem ((VOID *)(UINTN)(KBase + Bytes), Span - Bytes);
        }
        if (InitrdBuf != NULL && InitrdBytes > 0) {
          CopyMem ((VOID *)(UINTN)RAW_INITRD, InitrdBuf, InitrdBytes);
        }
        VOID *Tree = (VOID *)(UINTN)RAW_DTB;
        if (FdtOpenInto (DtbBuf, Tree, (INT32)TreeCapacity) != 0) {
          if (gST != NULL && gST->ConOut != NULL) Print (L"  FdtOpenInto failed\r\n");
          FreePool (DtbBuf); FreePool (Kernel); Root->Close (Root); continue;
        }
        // Patch the FDT: real /memory ranges, /chosen/bootargs, initrd, and a
        // simple-framebuffer node so the kernel console is visible on the panel.
        // Patch the FDT. libfdt node offsets are invalidated by earlier inserts
        // that shift the struct block: the old code resolved /chosen once and
        // then grew /memory's reg from 16 to 240 bytes, so FdtAddSubnode() saw a
        // stale parent (-4 FDT_ERR_BADOFFSET) and bootargs/initrd were written
        // through stale offsets. Resolve every path freshly and do the /memory
        // reg replacement last.
        {
          STATIC CONST CHAR8 Cmdline[] =
            "earlycon=efifb console=tty0 console=ttyMSM0,115200n8 keep_bootcon loglevel=8 ignore_loglevel nokaslr maxcpus=1 "
            "root=LABEL=sunuefi_root rootfstype=ext4 rootwait rw "
            "pd_ignore_unused clk_ignore_unused regulator_ignore_unused iommu.passthrough=1 panic=15";
#if ERHAI_DISABLE_UFS
          {
            INT32 Ufs = FdtPathOffset (Tree, "/soc/ufs@1d84000");
            if (Ufs >= 0) { FdtSetPropString (Tree, Ufs, "status", "disabled"); }
            INT32 UfsPhy = FdtPathOffset (Tree, "/soc/phy@1d80000");
            if (UfsPhy >= 0) { FdtSetPropString (Tree, UfsPhy, "status", "disabled"); }
            if (gST != NULL && gST->ConOut != NULL) {
              Print (L"  ufs disabled for isolation: hc %d phy %d\r\n", Ufs, UfsPhy);
            }
          }
#endif
#if ERHAI_DISABLE_SCM
          {
            INT32 Scm = FdtPathOffset (Tree, "/firmware/scm");
            if (Scm >= 0) { FdtSetPropString (Tree, Scm, "status", "disabled"); }
            if (gST != NULL && gST->ConOut != NULL) {
              Print (L"  scm disabled for isolation: node %d\r\n", Scm);
            }
          }
#endif
#if ERHAI_DISABLE_UART
          {
            INT32 Uart = FdtPathOffset (Tree, "/soc@0/geniqup@ac0000/serial@a9c000");
            if (Uart >= 0) { FdtSetPropString (Tree, Uart, "status", "disabled"); }
            if (gST != NULL && gST->ConOut != NULL) {
              Print (L"  uart7 disabled for isolation: node %d\r\n", Uart);
            }
          }
#endif
          INT32 Node = FdtPathOffset (Tree, "/chosen");
          if (Node >= 0) {
            FdtSetProp (Tree, Node, "bootargs", Cmdline, sizeof (Cmdline));
            if (InitrdBuf != NULL && InitrdBytes > 0) {
              UINT64 Start = RAW_INITRD, End = RAW_INITRD + InitrdBytes;
              FdtSetPropU64 (Tree, Node, "linux,initrd-start", Start);
              FdtSetPropU64 (Tree, Node, "linux,initrd-end", End);
            }
            // Raw boot has no EFI system table, so sysfb cannot discover the GOP.
            // Describe the active framebuffer explicitly; simpledrm + fbcon then
            // bring up an on-screen console.
            if (Gop != NULL && Gop->Mode != NULL && Gop->Mode->Info != NULL &&
                (Gop->Mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor ||
                 Gop->Mode->Info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor)) {
              UINT32 FbW = Gop->Mode->Info->HorizontalResolution;
              UINT32 FbH = Gop->Mode->Info->VerticalResolution;
              UINT32 FbStride = Gop->Mode->Info->PixelsPerScanLine * 4;
              UINT64 FbBase = Gop->Mode->FrameBufferBase;
              UINT64 FbBytes = (UINT64)FbStride * FbH;
              INT32 Fb = FdtAddSubnode (Tree, Node, "framebuffer@FC800000");
              if (Fb >= 0) {
                UINT32 Reg[4];
                Reg[0] = SwapBytes32 ((UINT32)(FbBase >> 32));
                Reg[1] = SwapBytes32 ((UINT32)FbBase);
                Reg[2] = SwapBytes32 ((UINT32)(FbBytes >> 32));
                Reg[3] = SwapBytes32 ((UINT32)FbBytes);
                UINT32 V = SwapBytes32 (FbW);
                FdtSetPropString (Tree, Fb, "compatible", "simple-framebuffer");
                FdtSetProp (Tree, Fb, "reg", Reg, sizeof (Reg));
                FdtSetProp (Tree, Fb, "width", &V, sizeof (V));
                V = SwapBytes32 (FbH);
                FdtSetProp (Tree, Fb, "height", &V, sizeof (V));
                V = SwapBytes32 (FbStride);
                FdtSetProp (Tree, Fb, "stride", &V, sizeof (V));
                FdtSetPropString (Tree, Fb, "format",
                  Gop->Mode->Info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor ?
                  "a8r8g8b8" : "a8b8g8r8");
              }
              // Resolve /reserved-memory only after the framebuffer subnode was
              // inserted; that insert shifts every following node offset.
              INT32 Rsv = FdtPathOffset (Tree, "/reserved-memory");
              if (Rsv >= 0) {
                INT32 FbRsv = FdtAddSubnode (Tree, Rsv, "framebuffer@FC800000");
                if (FbRsv >= 0) {
                  UINT32 Reg[4];
                  Reg[0] = 0;
                  Reg[1] = SwapBytes32 (0xFC800000U);
                  Reg[2] = 0;
                  Reg[3] = SwapBytes32 (0x02B00000U);
                  FdtSetProp (Tree, FbRsv, "reg", Reg, sizeof (Reg));
                }
              }
              if (gST != NULL && gST->ConOut != NULL) {
                Print (L"  framebuffer: %ux%u stride %u at %lx, dtb node %d\r\n",
                       FbW, FbH, FbStride, FbBase, Fb);
              }
            }
          }
          // /memory is part of the DTB now: the real ABL map is embedded at
          // build time (sm8750-oneplus-erhai.dts). Do not grow the property here;
          // that used to shift libfdt node offsets and corrupt /chosen writes.
          if (gST != NULL && gST->ConOut != NULL) {
            Print (L"  fdt patch: chosen %d (memory map is build-time)\r\n", Node);
          }
        }
        if (gST != NULL && gST->ConOut != NULL) {
          Print (L"  raw placement: kernel %lx, entry %lx, dtb %lx, initrd %lx\r\n",
                 KBase, KBase + TextOffset, (UINT64)RAW_DTB, (UINT64)RAW_INITRD);
        }
        Root->Close (Root);
        // The arm64 boot protocol requires the loaded image (and DTB/initrd) to be
        // cleaned to the PoC before the MMU goes off; the raw entry clears sctlr M/C
        // unconditionally. Clean everything we touched, plus this driver's own image
        // (it executes with the MMU off for a few instructions before the branch).
        {
          EFI_LOADED_IMAGE_PROTOCOL *Self = NULL;
          WriteBackInvalidateDataCacheRange ((VOID *)(UINTN)KBase, Span);
          WriteBackInvalidateDataCacheRange (Tree, TreeCapacity);
          if (InitrdBuf != NULL && InitrdBytes > 0) {
            WriteBackInvalidateDataCacheRange ((VOID *)(UINTN)RAW_INITRD, InitrdBytes);
          }
          if (!EFI_ERROR (gBS->HandleProtocol (gImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Self)) &&
              Self != NULL) {
            WriteBackInvalidateDataCacheRange ((VOID *)(UINTN)Self->ImageBase, (UINTN)Self->ImageSize);
          }
        }
        if (gST != NULL && gST->ConOut != NULL) {
          // The kernel's first firmware call is PSCI (psci_dt_init). Probe the
          // same SMC conduit here: a hang after this print localizes the fault
          // to PSCI/QHEE rather than the kernel image.
          register UINT64 PsciX0 __asm__("x0") = 0x84000000ULL; // PSCI_VERSION
          __asm__ volatile ("smc #0" : "+r" (PsciX0) : : "x1", "x2", "x3",
                            "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
                            "x12", "x13", "x14", "x15", "x16", "x17", "memory");
          Print (L"  psci version %lx\r\n", PsciX0);
          // The OnePlus ABL leaves the Gunyah/QHEE Apps Watchdog armed. The
          // mainline gunyah_wdt driver disables it at probe, but do it here as
          // well so it cannot bite between the raw jump and that probe.
          // SMCCC vendor-hyp 0x86000005, arg1=2 (BIT1|BIT0=0 -> disable).
          {
            register UINT64 GwX0 __asm__("x0") = 0x86000005ULL;
            register UINT64 GwX1 __asm__("x1") = 2ULL;
            register UINT64 GwX2 __asm__("x2") = 0;
            register UINT64 GwX3 __asm__("x3") = 0;
            __asm__ volatile ("smc #0" : "+r" (GwX0) : "r" (GwX1), "r" (GwX2), "r" (GwX3)
                              : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
                              "x12", "x13", "x14", "x15", "x16", "x17", "memory");
            Print (L"  gunyah wdt disable %lx\r\n", GwX0);
          }
          Print (L"Starting kernel (raw) at %lx...\r\n", KBase + TextOffset);
        }
        // Piano owner step: quiesce the UFS and retain its SMMU before handing
        // off. The erhai UFS driver installs the same PIANO_UFS_SHUTDOWN
        // protocol piano's product owners use.
        {
          ERHAI_UFS_SHUTDOWN *Shutdown = NULL;
          EFI_GUID Guid = ERHAI_UFS_SHUTDOWN_GUID;
          EFI_STATUS HaltStatus = gBS->LocateProtocol (&Guid, NULL, (VOID **)&Shutdown);
          if (!EFI_ERROR (HaltStatus) && Shutdown != NULL && Shutdown->Revision == 1 && Shutdown->Halt != NULL) {
            HaltStatus = Shutdown->Halt ();
          }
          if (gST != NULL && gST->ConOut != NULL) {
            Print (L"  ufs owner halt %r\r\n", HaltStatus);
          }
        }
#if ERHAI_RAW_EBS
        // --- ExitBootServices (one retry on stale map key) ---
        {
          UINTN MapSize = 0, MapKey = 0, DescSize = 0; UINT32 DescVersion = 0;
          EFI_STATUS MStatus = gBS->GetMemoryMap (&MapSize, NULL, &MapKey, &DescSize, &DescVersion);
          if (MStatus == EFI_BUFFER_TOO_SMALL && MapSize > 0) {
            UINTN Capacity = MapSize + 8 * DescSize;
            EFI_MEMORY_DESCRIPTOR *Map = AllocatePool (Capacity);
            if (Map != NULL) {
              for (UINTN Attempt = 0; Attempt < 2; ++Attempt) {
                MapSize = Capacity;
                MStatus = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVersion);
                if (EFI_ERROR (MStatus)) { break; }
                MStatus = gBS->ExitBootServices (gImageHandle, MapKey);
                if (!EFI_ERROR (MStatus)) { break; }
              }
            }
          }
          if (EFI_ERROR (MStatus)) {
            if (gST != NULL && gST->ConOut != NULL) Print (L"ExitBootServices %r\r\n", MStatus);
          }
        }
#else
        // Isolation build: vendor drivers register ExitBootServices callbacks and
        // one of them faults in the DXE heap. Enter the kernel without EBS to
        // determine whether the handoff itself (not the callback set) is broken.
        if (gST != NULL && gST->ConOut != NULL) {
          Print (L"  raw handoff: ExitBootServices skipped (isolation build)\r\n");
        }
#endif
        // EBS is skipped because the vendor EBS callbacks fault on absolute
        // pointers that were not rebased with the PE header. Stop the UEFI
        // watchdog and the arch timer manually so neither can fire after the
        // firmware has handed control to the kernel.
        {
          EFI_WATCHDOG_TIMER_ARCH_PROTOCOL *Wdt = NULL;
          EFI_TIMER_ARCH_PROTOCOL *Tmr = NULL;
          if (!EFI_ERROR (gBS->LocateProtocol (&gEfiWatchdogTimerArchProtocolGuid, NULL, (VOID **)&Wdt)) && Wdt != NULL) {
            Wdt->SetTimerPeriod (Wdt, 0);
            if (gST != NULL && gST->ConOut != NULL) { Print (L"  watchdog stopped\r\n"); }
          }
          if (!EFI_ERROR (gBS->LocateProtocol (&gEfiTimerArchProtocolGuid, NULL, (VOID **)&Tmr)) && Tmr != NULL) {
            Tmr->SetTimerPeriod (Tmr, 0);
          }
        }
        {
          UINT64 El;
          UINT64 Sctlr;
          __asm__ volatile ("mrs %0, CurrentEL" : "=r" (El));
          __asm__ volatile ("mrs %0, sctlr_el1" : "=r" (Sctlr));
          if (gST != NULL && gST->ConOut != NULL) {
            Print (L"  raw enter: EL%u sctlr %lx tree %lx entry %lx code %lx\r\n",
                   (UINT32)(El >> 2), Sctlr, (UINT64)(UINTN)Tree,
                   (UINT64)(UINTN)(KBase + TextOffset), (UINT64)(UINTN)RawEnter);
          }
          if ((El >> 2) == 1) {
            RawEnter ((UINT64)(UINTN)Tree, (UINT64)(UINTN)(KBase + TextOffset));
          }
          if (gST != NULL && gST->ConOut != NULL) {
            Print (L"  raw handoff requires EL1, staying in firmware\r\n");
          }
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
