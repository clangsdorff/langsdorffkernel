# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Novatek NT36xxx touchscreen driver.
#
TARGET = novatek_ts_nt36523
# Each configuration option enables a list of files.

$(TARGET)-objs	:= nt36xxx.o nt36xxx_fw_update.o nt36xxx_ext_proc.o
ccflags-$(CONFIG_SAMSUNG_TUI)	+= -Idrivers/misc/tui
obj-$(CONFIG_TOUCHSCREEN_NT36XXX) += nvt_36xxx.o

obj-$(CONFIG_TOUCHSCREEN_NOVATEK_NT36523_SPI) += $(TARGET).o

ccflags-y += -Wformat
nvt_36xxx-y := nt36xxx.o nt36xxx_fw_update.o nt36xxx_ext_proc.o nt36xxx_mp_ctrlram.o