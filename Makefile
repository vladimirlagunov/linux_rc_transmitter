# -*- GNUMakefile -*-
.DELETE_ON_ERROR:
KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
TARGET = rc_transmitter

obj-m := $(TARGET).o

ccflags-y += -Werror -Wall

default:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	@rm -f *.o .*.cmd .*.flags *.mod.c *.order
	@rm -f .*.*.cmd *~ *.*~ TODO.*
	@rm -fR .tmp*
	@rm -rf .tmp_versions
.PHONY: clean

disclean: clean
	@rm -f *.ko *.symvers
.PHONY: disclean
