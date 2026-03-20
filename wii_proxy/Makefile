#============================================================================
# wii_proxy/Makefile
#
# Builds the Wii Protocol Proxy homebrew application (.dol).
# Standard devkitPPC / wii_rules template.
#
# Prerequisites (installed via devkitPro pacman):
#   devkitPPC, libogc, libfat-ogc, gamecube-tools
#============================================================================

.SUFFIXES:

ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

#--------------------------------------------------------------------------
TARGET		:=	wii_proxy
BUILD		:=	build
SOURCES		:=	source
DATA		:=
INCLUDES	:=	source ../common

#--------------------------------------------------------------------------
CFLAGS		=	-g -O2 -Wall -Wextra $(MACHDEP) $(INCLUDE)
CXXFLAGS	=	$(CFLAGS)
LDFLAGS		=	-g $(MACHDEP) -Wl,-Map,$(notdir $@).map

#--------------------------------------------------------------------------
# Libraries: libogc (network + hardware), libfat (SD card config),
#            libbte (Bluetooth), libwiiuse (Wii Remote).
# Note: NiFi raw radio control is implemented through the Wii WD ioctl
# backend in wl_backend.c. The code talks to /dev/net/wd/command directly
# for beacon/link/raw send/raw recv.
#--------------------------------------------------------------------------
LIBS		:=	-lfat -lwiiuse -lbte -logc -lm

LIBDIRS		:=

#--------------------------------------------------------------------------
# Standard build rules — no edits needed below
#--------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT	:=	$(CURDIR)/$(TARGET)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
					$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
sFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

ifeq ($(strip $(CPPFILES)),)
	export LD	:=	$(CC)
else
	export LD	:=	$(CXX)
endif

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SOURCES	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) \
							$(sFILES:.s=.o) $(SFILES:.S=.o)
export OFILES		:=	$(OFILES_BIN) $(OFILES_SOURCES)
export HFILES		:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-iquote $(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					-I$(CURDIR)/$(BUILD) \
					-I$(LIBOGC_INC)

export LIBPATHS	:=	-L$(LIBOGC_LIB) $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo Cleaning...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).dol

else

DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

$(OFILES_SOURCES): $(HFILES)

-include $(DEPENDS)

endif
