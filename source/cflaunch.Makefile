# PS5 Vulkan compatibility probe - resident control payload build.
# Copyright (C) 2026 Mihawk-99
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds build/cflaunch/cflaunch.elf, the agent the console keeps resident so a
# probe battery can launch and close the runner title without a hand on the
# console. Run tools/build-ps5vkctl.sh, which resolves PS5_PAYLOAD_SDK.

ifndef PS5_PAYLOAD_SDK
$(error PS5_PAYLOAD_SDK is undefined; run tools/build-ps5vkctl.sh)
endif

include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk

ROOT := $(abspath $(CURDIR)/../..)
OUTPUT := $(ROOT)/build/cflaunch
ELF := $(OUTPUT)/cflaunch.elf

CFLAGS := -std=c11 -O2 -g -Wall -Wextra -Werror
LDLIBS := -lSceSystemService -lSceUserService -lSceNet -lSceSsl -lSceHttp2 -lSceHttp

all: $(ELF)

$(ELF): cflaunch.c | $(OUTPUT)
	$(CC) $(CFLAGS) -o $@ cflaunch.c $(LDLIBS)

$(OUTPUT):
	mkdir -p $(OUTPUT)

clean:
	rm -f $(ELF)

.PHONY: all clean
