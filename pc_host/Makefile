#============================================================================
# pc_host/Makefile
#
# Builds the DS Remote Desktop PC host server.
#
# Usage:
#   make              # Linux (default)
#   make OS=WIN       # Windows (MinGW / MSYS2)
#
# Dependencies:
#   Linux  : libx11-dev (for screen capture)
#   Windows: GDI (built-in), ws2_32 (built-in)
#   Optional: libssl-dev / openssl for HMAC verification
#============================================================================

TARGET   := dsrd_host
BUILD    := build
SRCDIR   := src
INCDIR   := include
COMMON   := ../common

CXX      ?= g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -I$(INCDIR) -I$(COMMON)

SRCS     := $(wildcard $(SRCDIR)/*.cpp)
OBJS     := $(patsubst $(SRCDIR)/%.cpp,$(BUILD)/%.o,$(SRCS))

# Platform detection
OS ?= $(shell uname -s)

ifeq ($(OS),WIN)
  # Windows (MinGW / MSYS2)
  CXXFLAGS += -D_WIN32
  LDFLAGS  += -lws2_32 -lgdi32
  TARGET   := $(TARGET).exe
else ifeq ($(OS),Windows_NT)
  CXXFLAGS += -D_WIN32
  LDFLAGS  += -lws2_32 -lgdi32
  TARGET   := $(TARGET).exe
else
  # Linux
  LDFLAGS  += -lX11 -lpthread
endif

# Optional OpenSSL for HMAC verification
ifeq ($(USE_OPENSSL),1)
  CXXFLAGS += -DHAS_OPENSSL
  LDFLAGS  += -lssl -lcrypto
endif

#--------------------------------------------------------------------------
.PHONY: all clean

all: $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: $(SRCDIR)/%.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) $(LDFLAGS) -o $@
	@echo "Built $@"

clean:
	rm -rf $(BUILD) $(TARGET)
