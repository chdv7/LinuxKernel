PWD := $(shell pwd)
KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build
MODULE := kernel_timer

.PHONY: make build clean load unload format check

make: build

build:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean

format:
	clang-format -i src/*.c

load: build
	sudo insmod $(MODULE).ko

unload:
	sudo rmmod $(MODULE)

check: build
	sudo ./check.sh
