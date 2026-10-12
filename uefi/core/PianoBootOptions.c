// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <PiDxe.h>
#include <Protocol/FirmwareVolume2.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include "Components/os-boot/PianoEspBootSource.h"

EFI_STATUS PianoRegisterLinuxBootOption(VOID) {
  EFI_STATUS Status=PianoEspBootPresent();
  DEBUG((DEBUG_WARN,"PIANO_LINUX_BOOT_OPTION stage=esp-file status=%r\n",Status));
  if(Status!=EFI_SUCCESS)return Status;
  EFI_GUID File={0x9ddefc13,0x4746,0x441b,{0x87,0xe9,0x4c,0xd3,0xe1,0x21,0xb0,0x84}};
  EFI_HANDLE *Volumes=NULL;UINTN Count=0;
  Status=gBS->LocateHandleBuffer(ByProtocol,&gEfiFirmwareVolume2ProtocolGuid,NULL,&Count,&Volumes);
  if(Status!=EFI_SUCCESS)return Status;
  Status=EFI_NOT_FOUND;
  for(UINTN I=0;I<Count;++I) {
    EFI_FIRMWARE_VOLUME2_PROTOCOL *Fv=NULL;VOID *Image=NULL;UINTN Bytes=0;UINT32 Auth=0;
    if(gBS->HandleProtocol(Volumes[I],&gEfiFirmwareVolume2ProtocolGuid,(VOID **)&Fv)!=EFI_SUCCESS||!Fv)continue;
    EFI_STATUS Read=Fv->ReadSection(Fv,&File,EFI_SECTION_PE32,0,&Image,&Bytes,&Auth);
    if(Image)FreePool(Image);
    if(Read!=EFI_SUCCESS||!Bytes)continue;
    EFI_DEVICE_PATH_PROTOCOL *Base=DevicePathFromHandle(Volumes[I]);
    if(!Base)continue;
    MEDIA_FW_VOL_FILEPATH_DEVICE_PATH Node;
    ZeroMem(&Node,sizeof(Node));Node.Header.Type=MEDIA_DEVICE_PATH;Node.Header.SubType=MEDIA_PIWG_FW_FILE_DP;
    SetDevicePathNodeLength(&Node.Header,sizeof(Node));Node.FvFileName=File;
    EFI_DEVICE_PATH_PROTOCOL *Path=AppendDevicePathNode(Base,&Node.Header);
    if(!Path){Status=EFI_OUT_OF_RESOURCES;break;}
    EFI_BOOT_MANAGER_LOAD_OPTION Option;
    Status=EfiBootManagerInitializeLoadOption(&Option,LoadOptionNumberUnassigned,LoadOptionTypeBoot,
      LOAD_OPTION_ACTIVE,L"Piano Linux",Path,NULL,0);
    FreePool(Path);
    if(Status==EFI_SUCCESS){Status=EfiBootManagerAddLoadOptionVariable(&Option,MAX_UINTN);EfiBootManagerFreeLoadOption(&Option);}
    break;
  }
  FreePool(Volumes);
  DEBUG((DEBUG_WARN,"PIANO_LINUX_BOOT_OPTION stage=register status=%r variables=ram\n",Status));
  return Status;
}
