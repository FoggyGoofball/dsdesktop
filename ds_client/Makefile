#============================================================================
# ds_client/Makefile
#
# DS Remote Desktop client — standard devkitARM NDS Makefile.
#
# Uses the devkitARM ds_rules template, which:
#   - Compiles ARM9 sources from SOURCES directories
#   - Links against libnds9, libdswifi9, and auto-links calico
#   - Bundles the calico default ARM7 binary (ds7_maine.elf)
#   - Produces a .nds ROM via ndstool
#
# Prerequisites (all installed via devkitPro pacman):
#   devkitARM, libnds, dswifi, calico, default-arm7, ndstool, dstools
#============================================================================

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

include $(DEVKITARM)/ds_rules

#--------------------------------------------------------------------------
# Project configuration
#--------------------------------------------------------------------------
TARGET		:=	dsremote
BUILD		:=	build
SOURCES		:=	arm9/source
DATA		:=
INCLUDES	:=	arm9/source ../common

GAME_TITLE	:=	DS Remote Desktop
GAME_SUBTITLE1	:=	NiFi streaming client
GAME_SUBTITLE2	:=	github.com/ds-remote

#--------------------------------------------------------------------------
# Code generation options
#--------------------------------------------------------------------------
ARCH	:=	-march=armv5te -mtune=arm946e-s -mthumb

CFLAGS	:=	-g -Wall -Wextra -O2 -ffunction-sections -fdata-sections \
			$(ARCH) $(INCLUDE) -DARM9

CXXFLAGS	:=	$(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS		:=	-g $(ARCH)
LDFLAGS		=	-specs=ds_arm9.specs -g $(ARCH) \
			-Wl,-Map,$(notdir $*.map) -Wl,--gc-sections

#--------------------------------------------------------------------------
# Libraries (order matters for the linker)
#
# -ldswifi9  : Wi-Fi / raw packet support
# -lnds9     : libnds (console, input, video, sound, DMA, cache, etc.)
#
# calico_ds9 is auto-linked by ds_rules when -lnds9 is present.
#--------------------------------------------------------------------------
LIBS	:=	-ldswifi9 -lnds9

#--------------------------------------------------------------------------
# Library search paths
#--------------------------------------------------------------------------
LIBDIRS	:=	$(LIBNDS)

#--------------------------------------------------------------------------
# Below this line is the standard devkitARM build machinery.
# You should not normally need to edit it.
#
# IMPORTANT: The workspace path must NOT contain spaces for MSYS make.
# If your path has spaces, create a junction:
#   cmd /c mklink /J C:\dsrd "C:\path with spaces\project"
# Then build from C:\dsrd\ds_client.
#--------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES	:=	$(addsuffix .o,$(BINFILES)) \
					$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo Cleaning...
	@rm -fr $(BUILD) $(TARGET).elf $(TARGET).nds

else

DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT).nds: $(OUTPUT).elf

$(OUTPUT).elf: $(OFILES)

-include $(DEPENDS)

endif
