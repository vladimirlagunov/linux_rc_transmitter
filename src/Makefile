# -*- GNUMakefile -*-
.DELETE_ON_ERROR:
KVERSION ?= $(shell uname -r)
KDIR := /lib/modules/$(KVERSION)
PWD := $(shell pwd)
TARGET = rc-transmitter

obj-m := $(TARGET).o

ccflags-y += -Werror -Wall

default:
	$(MAKE) -C $(KDIR)/build M=$(PWD) modules

clean:
	@rm -f *.o .*.cmd .*.flags *.mod.c *.order
	@rm -f .*.*.cmd *~ *.*~ TODO.*
	@rm -fR .tmp*
	@rm -rf .tmp_versions
.PHONY: clean

disclean: clean
	@rm -f *.ko *.symvers
.PHONY: disclean
