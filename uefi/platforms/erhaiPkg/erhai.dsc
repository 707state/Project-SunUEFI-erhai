##
#  Copyright (c) 2011 - 2022, ARM Limited. All rights reserved.
#  Copyright (c) 2014, Linaro Limited. All rights reserved.
#  Copyright (c) 2015 - 2020, Intel Corporation. All rights reserved.
#  Copyright (c) 2018, Bingxing Wang. All rights reserved.
#  Copyright (c) Microsoft Corporation.
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  PLATFORM_NAME                  = erhai
  PLATFORM_GUID                  = FFAAEA90-6A4C-5DD6-8B7C-49A504444594
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/erhaiPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = erhaiPkg/erhai.fdf
  USE_CUSTOM_DISPLAY_DRIVER      = 0

  #
  # 0 = SM8750-AB
  # 1 = SM8750-3-AB
  # 2 = SM8750-AC
  #
  SOC_TYPE                       = 0

!include PakalaPkg/PakalaPkg.dsc.inc

# DIAGNOSTIC overrides (used for both DEBUG and RELEASE-diag builds): framebuffer
# serial + BaseDebugLibSerialPort with all print levels. PropertyMask 0x2F enables
# asserts (exception reports with register context for crash triage);
# ClearMemory stays 0 — never poison memory the vendor drivers expect intact.
# For a pure-RELEASE build flip the mask to 0x00.
# RELEASE-DIAGNOSTIC overrides: keep the vendor drivers' RELEASE semantics (asserts
# compiled out — the vendor uefi_a is RELEASE_CLANG160LINUX and its drivers rely on
# non-fatal error paths) while RESTORING console output for bring-up: framebuffer
# serial + BaseDebugLibSerialPort with all print levels, PropertyMask 0 (no asserts,
# no deadloop), memory retained (no DEBUG ClearMemory poisoning).
[PcdsFixedAtBuild]
  gEfiMdePkgTokenSpaceGuid.PcdDebugPropertyMask|0x2F
  gEfiMdePkgTokenSpaceGuid.PcdDebugClearMemoryValue|0x00
  gEfiMdePkgTokenSpaceGuid.PcdDebugPrintErrorLevel|0x8007EE0F

[LibraryClasses]
  SerialPortLib|SiliciumPkg/Library/FrameBufferSerialPortLib/FrameBufferSerialPortLib.inf
  DebugLib|MdePkg/Library/BaseDebugLibSerialPort/BaseDebugLibSerialPort.inf

[PcdsFixedAtBuild]
  #
  # DDR Memory
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000

  #
  # UEFI Stack
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0xA760D000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"OnePlus"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"Pad 2 Pro"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"erhai"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"OPD2415"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"24924"

  #
  # Simple Frame Buffer
  #
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|3392
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2400
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32

  #
  # Platform PEI
  #
  gQcomPkgTokenSpaceGuid.PcdPlatformType|"LA"

[LibraryClasses]
  #
  # Memory Libraries
  #
  MemoryMapLib|erhaiPkg/Library/MemoryMapLib/MemoryMapLib.inf

  #
  # QCOM Libraries
  #
  ConfigurationMapLib|erhaiPkg/Library/ConfigurationMapLib/ConfigurationMapLib.inf

[Components]
  #
  # ACPI Tables
  #
  #erhai/AcpiTables.inf

# Stage 0: no persistent variables, storage, capsules or OS boot.
[PcdsFixedAtBuild]
  gEfiMdeModulePkgTokenSpaceGuid.PcdEmuVariableNvModeEnable|TRUE
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorPartNumber|"SM8750-AB"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosProcessorModel|"Snapdragon 8 Elite"
[LibraryClasses]
  DeviceBootManagerLib|erhaiPkg/Library/Stage0BootManagerLib/Stage0BootManagerLib.inf
[Components]
  erhaiPkg/Drivers/CapsuleArchNullDxe/CapsuleArchNullDxe.inf
  erhaiPkg/Drivers/ErhaiUfsDxe/ErhaiUfsDxe.inf
