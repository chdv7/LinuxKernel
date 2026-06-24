PWD := $(shell pwd)
KERNEL_DIR ?= /lib/modules/$(shell uname -r)/build
MODULE := kernel_alloc

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

archive:
	tar -czf student_kernel_alloc.tar.gz Makefile Kbuild src check.sh README.md
