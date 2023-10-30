###################################################################
#                                                                 #
#                  Copyright 2023 RackWare, Inc.                  #
#                                                                 #
#  This is an unpublished work, is confidential and proprietary   #
#  to RackWare as a trade secret and is not to be used or         #
#  disclosed except and to the extent expressly permitted in an   #
#  applicable RackWare license agreement.                         #
#                                                                 #
###################################################################
MODULE_NAME := szs_tracker
SRCS := szs_tracker_module.c socketpool.c ioctl_handler.c error_utils.c
KERNELVERSION ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KERNELVERSION)/build
obj-m += $(MODULE_NAME).o
$(MODULE_NAME)-y := $(SRCS:.c=.o)

KBUILD_CFLAGS += -Wno-declaration-after-statement -I$(PWD)
EXTRA_CFLAGS += -g

all:
	$(MAKE) -C $(KDIR) M=$(PWD)

install:
	install -o root -g root -m 0755 $(MODULE_NAME).ko /lib/modules/$(KERNELVERSION)/kernel/drivers/block/
	depmod -a

uninstall:
	rm -f /lib/modules/$(KERNELVERSION)/kernel/drivers/block/$(MODULE_NAME).ko
	depmod -a

clean:
	rm -rf *.o *.ko *.symvers *.mod *.mod.c .*.cmd Module.markers modules.order .tmp_versions .$(MODULE_NAME).o.d built-in.a
