PWD := $(shell pwd)
KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build
MODULE := kernel_hashtable_search

.PHONY: build clean format load unload check archive

build:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean

format:
	clang-format -i src/*.c src/*.h

load: build
	sudo insmod $(MODULE).ko

unload:
	sudo rmmod $(MODULE)

check: build
	sudo ./check.sh
