obj-m += rngdriver.o
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
	$(CC) -O2 -Wall -Wextra -o rng_user user.c

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(RM) rng_user

.PHONY: all clean
