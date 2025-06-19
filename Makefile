obj-m += ccsds123b2.o

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

all: ccsds123b2.h
	make -C $(KERNEL_SRC) M=$(PWD) modules

ccsds123b2.h:
	./gen_debugfs > ccsds123b2.h

clean:
	make -C $(KERNEL_SRC) M=$(PWD) clean
