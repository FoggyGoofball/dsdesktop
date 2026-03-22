#============================================================================
# ds_client/Makefile
#
# DS Remote Desktop client — BlocksDS combined build.
#
# ARM7: Uses BlocksDS pre-built arm7_dswifi.elf which includes
#       installWifiFIFO + Wifi_Update + sound + input + system FIFOs.
#       This is required for ARM9 dswifi calls to reach the hardware.
#
# ARM9: Custom build with dswifi9 raw 802.11 TX/RX.
#============================================================================

BLOCKSDS      ?= /opt/wonderful/thirdparty/blocksds/core
BLOCKSDSEXT   ?= /opt/wonderful/thirdparty/blocksds/external

NAME          := dsremote

GAME_TITLE    := DS Remote Desktop
GAME_SUBTITLE := NiFi streaming client
GAME_AUTHOR   := github.com/ds-remote
GAME_ICON     ?= $(BLOCKSDS)/sys/icon.gif

# BlocksDS pre-built ARM7 with dswifi7 support
ARM7_ELF      := $(BLOCKSDS)/sys/arm7/main_core/arm7_dswifi.elf

MAKE          := make
RM            := rm -rf

ROM           := $(NAME).nds

.PHONY: all clean arm9

all: $(ROM)

arm9:
	$(MAKE) -f Makefile.arm9 --no-print-directory

$(ROM): arm9
	@echo "  NDSTOOL $@"
	$(BLOCKSDS)/tools/ndstool/ndstool -c $@ \
		-7 $(ARM7_ELF) -9 build/arm9.elf \
		-b $(GAME_ICON) "$(GAME_TITLE);$(GAME_SUBTITLE);$(GAME_AUTHOR)"

clean:
	$(MAKE) -f Makefile.arm9 clean --no-print-directory
	$(RM) $(ROM) build
