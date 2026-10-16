# Copyright, Linaro Ltd, 2023
# SPDX-License-Identifier: BSD-3-Clause
dnl DEVICE_SG_ADD(stream, name, stream-id, stream-index, 
dnl 	format, min-rate, max-rate, min-channels, max-channels,
dnl	domain, stack-size,
dnl	interface-type, interface-index, sd-line-idx, data-format,
dnl	sg-iid-start, cont-iid-start, mod-iid-start
define(`DEVICE_SG_ADD',
`undefine(`DEVICE_NAME') dnl'
`undefine(`DEVICE_DAI_ID') dnl'
`undefine(`DEVICE_FORMAT') dnl'
`undefine(`DEVICE_MIN_RATE') dnl'
`undefine(`DEVICE_MAX_RATE') dnl'
`undefine(`DEVICE_MIN_CHANNELS') dnl'
`undefine(`DEVICE_MAX_CHANNELS') dnl'
`undefine(`DEVICE_INTF_TYPE') dnl'
`undefine(`DEVICE_INTF_INDEX') dnl'
`undefine(`DEVICE_DATA_FORMAT') dnl'
`undefine(`SG_IID_START') dnl'
`undefine(`CONT_IID_START') dnl'
`undefine(`MOD_IID_START') dnl'
`define(`DEVICE_NAME', ``$2'') dnl'
`define(`DEVICE_DAI_ID', `$3') dnl'
`define(`DEVICE_FORMAT', `$4') dnl'
`define(`DEVICE_MIN_RATE', `$5') dnl'
`define(`DEVICE_MAX_RATE', `$6') dnl'
`define(`DEVICE_MIN_CHANNELS', `$7') dnl'
`define(`DEVICE_MAX_CHANNELS', `$8') dnl'
`define(`DEVICE_INTF_TYPE', `$9') dnl'
`define(`DEVICE_INTF_INDEX', `$10') dnl'
`define(`DEVICE_SD_LINE_IDX', `$11') dnl'
`define(`DEVICE_DATA_FORMAT', `$12') dnl'
`define(`SG_IID_START', `$13') dnl'
`define(`CONT_IID_START', `$14') dnl'
`define(`MOD_IID_START', `$15') dnl'
`define(`NAME_PREFIX', `device$3') dnl'
`define(`MIXER_PREFIX', ``$16'') dnl'
`include($1)') dnl
