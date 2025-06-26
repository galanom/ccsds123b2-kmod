obj-m += ccsds123b2.o

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
SRC := $(shell pwd)

all: ccsds123b2.h
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) M=$(SRC) modules_install

ccsds123b2.h:
	./gen_debugfs > ccsds123b2.h

clean:
	make -C $(KERNEL_SRC) M=$(SRC) clean
