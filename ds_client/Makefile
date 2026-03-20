#============================================================================
# ds_client/Makefile
#
# DS Remote Desktop client — combined ARM7 + ARM9 build.
#
# This packages a custom ARM7 image together with the ARM9 client instead of
# relying on the auto-bundled default ARM7. The goal is maximum compatibility
# with older flashcart loaders that can freeze on some modern bundled images.
#============================================================================

.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

export TARGET := dsremote
export TOPDIR := $(CURDIR)

GAME_TITLE := DS Remote Desktop
GAME_SUBTITLE1 := NiFi streaming client
GAME_SUBTITLE2 := github.com/ds-remote

include $(DEVKITARM)/ds_rules

.PHONY: all checkarm7 checkarm9 clean

all: checkarm7 checkarm9 $(TARGET).nds

checkarm7:
	$(MAKE) -C arm7

checkarm9:
	$(MAKE) -C arm9

$(TARGET).nds : arm7/$(TARGET).elf arm9/$(TARGET).elf
	ndstool -c $(TARGET).nds -h 0x200 -7 arm7/$(TARGET).elf -9 arm9/$(TARGET).elf \
	-b $(GAME_ICON) "$(GAME_TITLE);$(GAME_SUBTITLE1);$(GAME_SUBTITLE2)"
	@echo built ... $(notdir $@)

arm7/$(TARGET).elf:
	$(MAKE) -C arm7

arm9/$(TARGET).elf:
	$(MAKE) -C arm9

clean:
	$(MAKE) -C arm9 clean
	$(MAKE) -C arm7 clean
	@echo Cleaning...
	@rm -f $(TARGET).nds
