// Minimal TX-only Qualcomm GENI UART SerialPortLib for erhai (OnePlus Pad 2 Pro).
//
// The debug UART (QUPv3_1 SE @ 0x00A9C000, qcom,geni-debug-uart) is initialized and
// clocked by XBL/ABL before UEFI is entered — this library only polls the serial
// engine and pushes bytes. No init, no RX: bring-up diagnostics only.
//
// Register map per the mainline qcom_geni_serial driver:
//   SE_GENI_S_CMD0         +0x30   serial command 0
//   SE_GENI_S_CMD_CTRL     +0x34
//   SE_GENI_STATUS         +0x40   bit1 = M_GENI_CMD_ACTIVE? (S activity bit0)
//   SE_GENI_TX_FIFO        +0x183c TX FIFO word 0
//   SE_IRQ_EN              +0xe24  keep clear: polled mode
#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/SerialPortLib.h>
#include <Library/IoLib.h>

#define GENI_UART_BASE  FixedPcdGet64 (PcdGeniUartBase)

#define GENI_S_CMD0     0x30
#define GENI_STATUS     0x40
#define GENI_TX_FIFO    0x183c

// Command opcode for UART single TX: UART_START_TX = 0x1; length in bits 12..17
// is honored by the firmware. One byte per command keeps it simple.
#define UART_START_TX   0x1

STATIC BOOLEAN mUartEnabled = FALSE;
STATIC BOOLEAN mChecked = FALSE;

STATIC BOOLEAN UartAlive (VOID)
{
  UINT32 Status;
  if (!mChecked) {
    mChecked = TRUE;
    // A configured GENI engine has a non-zero configuration; an unclocked
    // peripheral reads back as 0 or faults. Treat a plausible status word as alive.
    Status = MmioRead32 (GENI_UART_BASE + GENI_STATUS);
    mUartEnabled = (Status != 0xFFFFFFFF) && (Status != 0);
  }
  return mUartEnabled;
}

RETURN_STATUS EFIAPI SerialPortInitialize (VOID)
{
  return RETURN_SUCCESS;
}

STATIC VOID GeniTxByte (UINT8 Byte)
{
  UINT32 Iterations = 0;

  // Wait for the engine to finish any previous serial command.
  while ((MmioRead32 (GENI_UART_BASE + GENI_STATUS) & BIT0) != 0) {
    if (++Iterations > 100000) {
      return; // engine wedged: drop the byte rather than hang bring-up
    }
  }

  MmioWrite32 (GENI_UART_BASE + GENI_TX_FIFO, Byte);
  MmioWrite32 (GENI_UART_BASE + GENI_S_CMD0,
               UART_START_TX | ((UINT32)1 << 12)); // START_TX, length = 1
}

UINTN EFIAPI SerialPortWrite (IN UINT8 *Buffer, IN UINTN NumberOfBytes)
{
  UINTN Index;

  if (!UartAlive ()) {
    return 0;
  }

  for (Index = 0; Index < NumberOfBytes; Index++) {
    GeniTxByte (Buffer[Index]);
  }
  return NumberOfBytes;
}

UINTN EFIAPI SerialPortRead (OUT UINT8 *Buffer, IN UINTN NumberOfBytes)
{
  (VOID)Buffer;
  return 0; // TX-only diagnostics
}

BOOLEAN EFIAPI SerialPortPoll (VOID)
{
  return FALSE;
}

RETURN_STATUS EFIAPI SerialPortSetAttributes (
  IN OUT UINT64             *BaudRate,
  IN OUT UINT32             *ReceiveFifoDepth,
  IN OUT UINT32             *Timeout,
  IN OUT EFI_PARITY_TYPE    *Parity,
  IN OUT UINT8              *DataBits,
  IN OUT EFI_STOP_BITS_TYPE *StopBits)
{
  (VOID)BaudRate; (VOID)ReceiveFifoDepth; (VOID)Timeout; (VOID)Parity; (VOID)DataBits; (VOID)StopBits;
  return RETURN_SUCCESS; // ABL-configured line settings stay untouched
}

RETURN_STATUS EFIAPI SerialPortSetControl (IN UINT32 Control)
{
  (VOID)Control;
  return RETURN_SUCCESS;
}

RETURN_STATUS EFIAPI SerialPortGetControl (OUT UINT32 *Control)
{
  if (Control != NULL) {
    *Control = 0;
  }
  return RETURN_SUCCESS;
}
