// SPDX-License-Identifier: BSD-2-Clause-Patent
// Private text-console surface. The published GOP and physical mode remain
// unchanged; only GraphicsConsole's character rendering uses logical pixels.
typedef struct {
  EFI_GRAPHICS_OUTPUT_PROTOCOL Gop;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Info;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Physical;
  UINTN Scale;
} PIANO_CONSOLE_SURFACE;

STATIC EFI_STATUS EFIAPI PianoConsoleQuery(EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  UINT32 Mode,UINTN *Bytes,EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info){
  PIANO_CONSOLE_SURFACE *S=(VOID *)This;
  if(!Bytes||!Info||Mode!=S->Mode.Mode)return EFI_UNSUPPORTED;
  *Info=AllocateCopyPool(sizeof(S->Info),&S->Info);
  if(!*Info)return EFI_OUT_OF_RESOURCES;
  *Bytes=sizeof(S->Info);return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI PianoConsoleSet(EFI_GRAPHICS_OUTPUT_PROTOCOL *This,UINT32 Mode){
  PIANO_CONSOLE_SURFACE *S=(VOID *)This;
  return Mode==S->Mode.Mode?EFI_SUCCESS:EFI_UNSUPPORTED;
}
STATIC EFI_STATUS EFIAPI PianoConsoleBlt(EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Buffer,EFI_GRAPHICS_OUTPUT_BLT_OPERATION Op,
  UINTN Sx,UINTN Sy,UINTN Dx,UINTN Dy,UINTN W,UINTN H,UINTN Delta){
  PIANO_CONSOLE_SURFACE *S=(VOID *)This;UINTN N=S->Scale;
  if(!W||!H||Op>=EfiGraphicsOutputBltOperationMax)return EFI_INVALID_PARAMETER;
  if(Op!=EfiBltVideoToBltBuffer &&
     (Dx>=S->Info.HorizontalResolution||Dy>=S->Info.VerticalResolution||
      W>S->Info.HorizontalResolution-Dx||H>S->Info.VerticalResolution-Dy))
    return EFI_INVALID_PARAMETER;
  if(Op==EfiBltVideoFill)
    return S->Physical->Blt(S->Physical,Buffer,Op,0,0,Dx*N,Dy*N,W*N,H*N,0);
  if(Op==EfiBltVideoToVideo)
    return S->Physical->Blt(S->Physical,NULL,Op,Sx*N,Sy*N,Dx*N,Dy*N,W*N,H*N,0);
  if(!Buffer||W>MAX_UINTN/N||H>MAX_UINTN/N)return EFI_INVALID_PARAMETER;
  UINTN RowPixels=W*N,Rows=H*N;
  if(RowPixels>MAX_UINTN/sizeof(*Buffer)/Rows)return EFI_BAD_BUFFER_SIZE;
  if(Delta==0)Delta=W*sizeof(*Buffer);
  if(Delta%sizeof(*Buffer)||Delta/sizeof(*Buffer)<(Op==EfiBltBufferToVideo?Sx:Dx)+W)
    return EFI_INVALID_PARAMETER;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Scaled=AllocatePool(RowPixels*Rows*sizeof(*Buffer));
  if(!Scaled)return EFI_OUT_OF_RESOURCES;
  EFI_STATUS Status;
  if(Op==EfiBltBufferToVideo){
    for(UINTN Y=0;Y<Rows;++Y){
      EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Source=(VOID *)((UINT8 *)Buffer+(Sy+Y/N)*Delta);
      for(UINTN X=0;X<RowPixels;++X)Scaled[Y*RowPixels+X]=Source[Sx+X/N];
    }
    Status=S->Physical->Blt(S->Physical,Scaled,Op,0,0,Dx*N,Dy*N,RowPixels,Rows,RowPixels*sizeof(*Buffer));
  }else{
    Status=S->Physical->Blt(S->Physical,Scaled,Op,Sx*N,Sy*N,0,0,RowPixels,Rows,RowPixels*sizeof(*Buffer));
    if(!EFI_ERROR(Status))for(UINTN Y=0;Y<H;++Y){
      EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Dest=(VOID *)((UINT8 *)Buffer+(Dy+Y)*Delta);
      for(UINTN X=0;X<W;++X)Dest[Dx+X]=Scaled[Y*N*RowPixels+X*N];
    }
  }
  FreePool(Scaled);return Status;
}
STATIC EFI_STATUS PianoConsoleAttach(GRAPHICS_CONSOLE_DEV *Private,UINT32 *Width,UINT32 *Height){
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Physical=Private->GraphicsOutput;
  if(!Physical||*Width!=3200||*Height!=2136)return EFI_SUCCESS;
  PIANO_CONSOLE_SURFACE *S=AllocateZeroPool(sizeof(*S));
  if(!S)return EFI_OUT_OF_RESOURCES;
  S->Physical=Physical;S->Scale=4;
  S->Mode=*Physical->Mode;S->Info=*Physical->Mode->Info;
  S->Info.HorizontalResolution=*Width/4;S->Info.VerticalResolution=*Height/4;
  S->Info.PixelsPerScanLine=S->Info.HorizontalResolution;
  S->Info.PixelFormat=PixelBltOnly;
  S->Mode.Info=&S->Info;S->Mode.SizeOfInfo=sizeof(S->Info);
  S->Mode.FrameBufferBase=0;S->Mode.FrameBufferSize=0;
  S->Gop=(EFI_GRAPHICS_OUTPUT_PROTOCOL){PianoConsoleQuery,PianoConsoleSet,PianoConsoleBlt,&S->Mode};
  Private->GraphicsOutput=&S->Gop;Private->PianoConsoleSurface=S;
  *Width=S->Info.HorizontalResolution;*Height=S->Info.VerticalResolution;
  DEBUG((DEBUG_INFO,"PIANO_CONSOLE_SCALE factor=4 physical=3200x2136 logical=800x534\n"));
  return EFI_SUCCESS;
}
STATIC VOID PianoConsoleDetach(GRAPHICS_CONSOLE_DEV *Private){
  PIANO_CONSOLE_SURFACE *S=Private->PianoConsoleSurface;
  if(S){Private->GraphicsOutput=S->Physical;Private->PianoConsoleSurface=NULL;FreePool(S);}
}
