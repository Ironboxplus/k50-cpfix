# SPDX-License-Identifier: GPL-2.0
# Build against the DDK kdir (ghcr.io/ylarod/ddk-min:android12-5.10-*), as tiann/KernelSU kernel/Makefile does.
# The CRCs come from the running kernel: SYMVERS replaces $(KDIR)/Module.symvers,
# EXTRA_SYMVERS supplies the votable API of mtk_charger_framework.
KDIR ?= $(KERNEL_SRC)
MDIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
SYMVERS ?= $(MDIR)/../device/vmlinux-12292661.symvers
EXTRA_SYMVERS ?= $(MDIR)/../device/mtk_charger_framework.symvers
CPFIX_VERSION ?= $(shell git -C $(MDIR) describe --always --dirty 2>/dev/null || echo unknown)

$(info -- KDIR: $(KDIR))
$(info -- MDIR: $(MDIR))
$(info -- SYMVERS: $(SYMVERS))
$(info -- CPFIX_VERSION: $(CPFIX_VERSION))

.PHONY: all clean

all:
	cp $(SYMVERS) $(KDIR)/Module.symvers
	$(MAKE) -C $(KDIR) M=$(MDIR) KBUILD_EXTRA_SYMBOLS=$(EXTRA_SYMVERS) \
		CPFIX_VERSION=$(CPFIX_VERSION) modules -j$(shell nproc)

clean:
	$(MAKE) -C $(KDIR) M=$(MDIR) clean
