# OC OS — сборка системы с нуля.
#
#   make            собрать образ build/oc.img
#   make run        запустить в QEMU (если установлен)
#   make test       прогнать автотесты (unicorn-эмулятор, python3)
#   make clean

BUILD   := build
CC      := gcc
LD      := ld
OBJCOPY := objcopy
PYTHON  := python3

# QEMU: системный или локальный (tools/qemu/bin/qemu-system-x86_64)
QEMU ?= $(shell command -v qemu-system-x86_64 2>/dev/null || echo qemu-system-x86_64)

CFLAGS := -std=c11 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
          -m64 -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
          -mcmodel=small -O2 -Wall -Wextra \
          -Iinclude -Ikernel/include -Ikernel/src
ASFLAGS := $(CFLAGS)

KCFLAGS := $(CFLAGS)

# font8x16.c генерируется (scripts/fontgen.py) и может отсутствовать при
# разборе wildcard — добавляем явно, $(sort) убирает дубль
KERNEL_C_SRCS := $(sort $(wildcard kernel/src/*.c) kernel/src/font8x16.c)
KERNEL_S_SRCS := kernel/src/entry.S kernel/src/isr.S
KERNEL_OBJS   := $(patsubst kernel/src/%.c,$(BUILD)/k/%.o,$(KERNEL_C_SRCS)) \
                 $(patsubst kernel/src/%.S,$(BUILD)/k/%.o,$(KERNEL_S_SRCS))

HEADERS := $(wildcard include/*.h) $(wildcard kernel/include/*.h)

.PHONY: all clean run test font flash

all: $(BUILD)/oc.img

# ---------- запись на флешку (с защитой вашей системы!) ----------
# пример: make flash DEV=/dev/sdX   (только USB-фleshка; см. scripts/flash.sh)
flash: $(BUILD)/oc.img
	@test -n "$(DEV)" || { \
	    echo "использование: make flash DEV=/dev/sdX"; \
	    echo "  (DEV — ЦЕЛАЯ флешка, не раздел и не системный диск!)"; \
	    echo "  список: lsblk -o NAME,SIZE,MODEL,TRAN,MOUNTPOINT"; exit 1; }
	bash scripts/flash.sh $(BUILD)/oc.img $(DEV)

# ---------- ядро ----------

$(BUILD)/k:
	@mkdir -p $(BUILD)/k

$(BUILD)/k/%.o: kernel/src/%.c $(HEADERS) | $(BUILD)/k
	$(CC) $(KCFLAGS) -c -o $@ $<

$(BUILD)/k/%.o: kernel/src/%.S $(HEADERS) | $(BUILD)/k
	$(CC) $(ASFLAGS) -c -o $@ $<

$(BUILD)/kernel.elf: $(KERNEL_OBJS) kernel/linker.ld
	$(LD) -m elf_x86_64 -z noexecstack -T kernel/linker.ld -o $@ $(KERNEL_OBJS) -Map $(BUILD)/kernel.map

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@sz=$$(stat -c %s $@); \
	max=$$((448 * 1024)); \
	echo "kernel.bin: $$sz байт (лимит $$max)"; \
	test $$sz -le $$max || (echo "ядро не влезает в площадку загрузчика!"; exit 1)

# ---------- шрифт ----------

font: kernel/src/font8x16.c

kernel/src/font8x16.c: scripts/fontgen.py kernel/include/font.h
	$(PYTHON) scripts/fontgen.py > $@

# ---------- загрузчик ----------

$(BUILD)/stage1.o: boot/stage1.S $(HEADERS)
	@mkdir -p $(BUILD)
	$(CC) -m32 -nostdlib -ffreestanding -Iinclude -c -o $@ $<

$(BUILD)/stage1.bin: $(BUILD)/stage1.o
	$(LD) -m elf_i386 -Ttext 0x7C00 --oformat binary -o $@ $<
	@sz=$$(stat -c %s $@); test $$sz -le 512 || (echo "stage1 > 512 байт"; exit 1)

# stage2 собирается после ядра: в него зашиваются KERNEL_SECTORS/KERNEL_BYTES
$(BUILD)/stage2.o: boot/stage2.S $(HEADERS) $(BUILD)/kernel.bin
	@mkdir -p $(BUILD)
	ksz=$$(stat -c %s $(BUILD)/kernel.bin); \
	ksec=$$(( (ksz + 511) / 512 )); \
	$(CC) -m32 -nostdlib -ffreestanding -Iinclude -c \
	    -DKERNEL_SECTORS=$$ksec -DKERNEL_BYTES=$$ksz -o $@ $<

$(BUILD)/stage2.bin: $(BUILD)/stage2.o
	$(LD) -m elf_i386 -Ttext 0x8000 --oformat binary -o $@ $<
	@sz=$$(stat -c %s $@); test $$sz -le 16384 || (echo "stage2 > 16KiB"; exit 1)

# ---------- образ ----------

# UEFI-загрузчик (ваш ноутбук грузится именно так; нужен ziglang в .pydeps)
$(BUILD)/BOOTX64.EFI: boot/efi.c include/boot.h include/layout.h
	PYTHONPATH=.pydeps $(PYTHON) -m ziglang cc -target x86_64-windows-gnu -O2 \
	    -ffreestanding -fno-stack-protector -fno-builtin -mno-stack-arg-probe \
	    -Iinclude -Wl,--subsystem,efi_application boot/efi.c -o $@ -nostdlib

$(BUILD)/oc.img: $(BUILD)/stage1.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin \
                 $(BUILD)/BOOTX64.EFI scripts/mkimage.py
	$(PYTHON) scripts/mkimage.py $(BUILD)/stage1.bin $(BUILD)/stage2.bin \
	    $(BUILD)/kernel.bin $@ $(BUILD)/BOOTX64.EFI

# ---------- запуск / тесты ----------

run: $(BUILD)/oc.img
	$(QEMU) -machine pc -m 128M -drive format=raw,file=$(BUILD)/oc.img \
	    -serial stdio -no-reboot -no-shutdown

run-curses: $(BUILD)/oc.img
	$(QEMU) -machine pc -m 128M -drive format=raw,file=$(BUILD)/oc.img \
	    -display curses -serial stdio -no-reboot

test: $(BUILD)/oc.img
	$(PYTHON) tests/smoke.py $(BUILD)/oc.img

# ---------- запись / установщик ----------

.PHONY: setup
# установщик Windows (нужен ziglang: pip3 install --target .pydeps ziglang)
setup: dist/OC-Setup.exe
dist/OC-Setup.exe: installer/installer.c installer/oc_img.h
	PYTHONPATH=.pydeps python3 -m ziglang cc -target x86_64-windows-gnu -O2 \
	    -finput-charset=utf-8 -Wl,--subsystem,windows installer/installer.c -o $@ \
	    -luser32 -lshell32 -ladvapi32 -lgdi32 -Wl,--strip-all
installer/oc_img.h: dist/oc-v0.1.img installer/gen_img_header.py
	$(PYTHON) installer/gen_img_header.py dist/oc-v0.1.img installer/oc_img.h

clean:
	rm -rf $(BUILD) kernel/src/font8x16.c
