PWD := $(shell pwd)
KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build
MODULE := vmodem

.PHONY: all build clean format load unload check

all: build

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
