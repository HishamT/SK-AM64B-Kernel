obj-m += hello_char.o
obj-m += msp430_spi.o

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	make -C $(KDIR) M="$(PWD)" modules

clean:
	make -C $(KDIR) M="$(PWD)" clean