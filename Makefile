# =============================================================================
# Конфигурация
# =============================================================================

ARCH = x86_64

GNU_EFI_DIR = /usr
GNU_EFI_INC = $(GNU_EFI_DIR)/include/efi
GNU_EFI_LIB = $(GNU_EFI_DIR)/lib

# Путь к OVMF (BIOS для QEMU)
OVMF = /usr/share/edk2-ovmf/x64/OVMF.4m.fd

# Настройки для записи на флешку (ОСТОРОЖНО!)
USB_DEV = /dev/sda1
MOUNT_POINT = /mnt/usb_efi

CRT0 = $(GNU_EFI_LIB)/crt0-efi-$(ARCH).o
LDS = $(GNU_EFI_LIB)/elf_$(ARCH)_efi.lds

SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
RUN_DIR = run_dir
PROGRAMS_DIR = programs

TARGET = $(BUILD_DIR)/bootx64.efi
SO_TARGET = $(BUILD_DIR)/kernel.so

# =============================================================================
# Инструменты
# =============================================================================

CC = gcc
AS = nasm
LD = ld
OBJCOPY = objcopy

# =============================================================================
# Флаги Ядра (EFI)
# =============================================================================

CPPFLAGS = \
    -I$(INC_DIR) \
    -I$(GNU_EFI_INC) \
    -I$(GNU_EFI_INC)/$(ARCH) \
    -I$(GNU_EFI_INC)/protocol \
    -DGNU_EFI_USE_MS_ABI

CFLAGS = \
    -ffreestanding \
    -fno-stack-protector \
    -fno-stack-check \
    -fshort-wchar \
    -mno-red-zone \
    -maccumulate-outgoing-args \
    -Wall \
    -Wextra \
    -std=c11 \
    -O2 \
    -DEFI_FUNCTION_WRAPPER \
    -MMD -MP \
	-fPIC

# Флаги для NASM (64-bit ELF формат)
ASFLAGS = \
    -f elf64 \
    -I$(INC_DIR)/

# Альтернатива: если используете GAS (GNU Assembler)
# AS = as
# ASFLAGS = --64

LDFLAGS = \
    -nostdlib \
    -T $(LDS) \
    -shared \
    -Bsymbolic \
    -L$(GNU_EFI_LIB) \
    -znocombreloc

LIBS = -lefi -lgnuefi

# =============================================================================
# Флаги Пользовательских Программ (Userspace)
# =============================================================================

USER_CFLAGS = \
    -m64 \
    -nostdlib \
    -ffreestanding \
    -static \
    -fno-pie \
    -no-pie \
    -fno-stack-protector \
    -fno-stack-check \
    -mno-red-zone \
    -Wl,-Ttext=0x400000 \
    -Wl,--build-id=none \
    -O2 \
    -Wall \
    -I$(INC_DIR)

USER_ASFLAGS = \
    -f elf64

# =============================================================================
# Исходные файлы
# =============================================================================

# Ядро - C файлы
SRCS_C = $(shell find $(SRC_DIR) -name '*.c')
OBJS_C = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS_C))

# Ядро - ASM файлы (NASM синтаксис, расширение .asm)
SRCS_ASM = $(shell find $(SRC_DIR) -name '*.asm')
OBJS_ASM = $(patsubst $(SRC_DIR)/%.asm,$(OBJ_DIR)/%.asm.o,$(SRCS_ASM))

# Ядро - ASM файлы (GAS синтаксис, расширение .S или .s)
SRCS_S = $(shell find $(SRC_DIR) -name '*.S' -o -name '*.s')
OBJS_S = $(patsubst $(SRC_DIR)/%.S,$(OBJ_DIR)/%.S.o,$(patsubst $(SRC_DIR)/%.s,$(OBJ_DIR)/%.s.o,$(SRCS_S)))

# Все объектные файлы ядра
OBJS = $(OBJS_C) $(OBJS_ASM) $(OBJS_S)
DEPS = $(OBJS_C:.o=.d)

# Программы (все .c файлы в папке programs)
PROG_SRCS_C = $(wildcard $(PROGRAMS_DIR)/*.c)
PROG_SRCS_ASM = $(wildcard $(PROGRAMS_DIR)/*.asm)
PROG_BINS_C = $(patsubst $(PROGRAMS_DIR)/%.c,$(RUN_DIR)/%.elf,$(PROG_SRCS_C))
PROG_BINS_ASM = $(patsubst $(PROGRAMS_DIR)/%.asm,$(RUN_DIR)/%.elf,$(PROG_SRCS_ASM))
PROG_BINS = $(PROG_BINS_C) $(PROG_BINS_ASM)

# =============================================================================
# Правила
# =============================================================================

.PHONY: clean all dirs debug check-format run disk-img write_to_disk writer programs

all: dirs $(TARGET) programs

dirs:
	@mkdir -p $(BUILD_DIR)
	@mkdir -p $(OBJ_DIR)
	@mkdir -p $(RUN_DIR)/EFI/BOOT
	@mkdir -p $(PROGRAMS_DIR)

# --- Сборка Ядра ---

$(TARGET): $(SO_TARGET)
	$(OBJCOPY) \
		-j .text \
		-j .sdata \
		-j .data \
		-j .rodata \
		-j .dynamic \
		-j .dynsym \
		-j .rel \
		-j .rela \
		-j .rel.* \
		-j .rela.* \
		-j .reloc \
		--output-target=efi-app-$(ARCH) \
		$< $@
	@echo "=== Ядро собрано: $@ ==="

$(SO_TARGET): $(OBJS)
	$(LD) $(LDFLAGS) $(CRT0) $^ -o $@ $(LIBS)

# Компиляция C файлов ядра
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Компиляция NASM файлов ядра (.asm)
$(OBJ_DIR)/%.asm.o: $(SRC_DIR)/%.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

# Компиляция GAS файлов ядра (.S - с препроцессором)
$(OBJ_DIR)/%.S.o: $(SRC_DIR)/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

# Компиляция GAS файлов ядра (.s - без препроцессора)
$(OBJ_DIR)/%.s.o: $(SRC_DIR)/%.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# --- Сборка Программ ---

programs: $(PROG_BINS)

# Правило для компиляции C программ из programs/ в run_dir/
$(RUN_DIR)/%.elf: $(PROGRAMS_DIR)/%.c
	@echo "Компиляция C программы: $< -> $@"
	$(CC) $(USER_CFLAGS) -o $@ $<

# Правило для компиляции ASM программ из programs/ в run_dir/
$(RUN_DIR)/%.elf: $(PROGRAMS_DIR)/%.asm
	@echo "Компиляция ASM программы: $< -> $@"
	$(AS) $(USER_ASFLAGS) $< -o $(BUILD_DIR)/$*.o
	$(LD) -m elf_x86_64 -static -Ttext=0x400000 --build-id=none -o $@ $(BUILD_DIR)/$*.o

# =============================================================================
# Тестирование и Запуск
# =============================================================================

boot.img: dirs $(TARGET) programs
	@echo "Creating boot.img (64MB)..."
	@dd if=/dev/zero of=boot.img bs=1M count=64 status=none
	@parted -s boot.img mklabel gpt
	@parted -s boot.img mkpart primary fat32 1MiB 100%
	@parted -s boot.img set 1 esp on
	@sudo losetup -P -f boot.img
	@LOOP_DEV=$$(losetup -j boot.img | cut -d: -f1); \
	 sudo mkfs.vfat -F 32 -n "MINIOS" $${LOOP_DEV}p1; \
	 sudo mkdir -p mnt_img; \
	 sudo mount $${LOOP_DEV}p1 mnt_img; \
	 sudo mkdir -p mnt_img/EFI/BOOT; \
	 sudo cp $(TARGET) mnt_img/EFI/BOOT/BOOTX64.EFI; \
	 if [ -f bootlogo.bmp ]; then sudo cp bootlogo.bmp mnt_img/; fi; \
	 if ls $(RUN_DIR)/*.elf 1> /dev/null 2>&1; then sudo cp $(RUN_DIR)/*.elf mnt_img/; fi; \
	 sudo umount mnt_img; \
	 sudo losetup -d $${LOOP_DEV}; \
	 rm -rf mnt_img

run: boot.img
	qemu-system-x86_64 \
		-nodefaults \
		-vga std \
		-display gtk \
		-m 512M \
		-bios $(OVMF) \
		-device qemu-xhci \
		-device usb-mouse \
		-device usb-kbd \
		-drive file=boot.img,if=none,format=raw,id=boot_drive \
		-device nvme,drive=boot_drive,serial=boot \
		-serial stdio

# =============================================================================
# Запись на реальное железо
# =============================================================================

write_to_disk: $(TARGET) programs
	@echo "Writing to $(USB_DEV)..."
	@sudo mkdir -p $(MOUNT_POINT)
	@if mount | grep $(MOUNT_POINT) > /dev/null; then sudo umount $(MOUNT_POINT); fi
	@sudo mount $(USB_DEV) $(MOUNT_POINT)
	@sudo mkdir -p $(MOUNT_POINT)/EFI/BOOT
	@sudo cp $(TARGET) $(MOUNT_POINT)/EFI/BOOT/BOOTX64.EFI
	@if [ -f bootlogo.bmp ]; then sudo cp bootlogo.bmp $(MOUNT_POINT)/; fi
	@# --- ИСПРАВЛЕННАЯ СТРОКА ---
	@for f in $(RUN_DIR)/*.elf; do \
		if [ -f "$$f" ]; then \
			sudo cp "$$f" $(MOUNT_POINT)/; \
		fi; \
	done
	@echo "Syncing..."
	@sudo sync
	@sudo umount $(MOUNT_POINT)
	@echo "Done! Safe to remove."

writer: clean all write_to_disk

# =============================================================================
# Утилиты
# =============================================================================

debug:
	@echo "=== Проверка путей ==="
	@ls -la $(CRT0) 2>/dev/null || echo "CRT0 не найден: $(CRT0)"
	@ls -la $(LDS) 2>/dev/null || echo "LDS не найден: $(LDS)"
	@ls -la $(OVMF) 2>/dev/null || echo "OVMF не найден: $(OVMF)"
	@echo ""
	@echo "=== Исходные файлы ==="
	@echo "C файлы:   $(SRCS_C)"
	@echo "ASM файлы: $(SRCS_ASM)"
	@echo "S файлы:   $(SRCS_S)"
	@echo ""
	@echo "=== Объектные файлы ==="
	@echo "$(OBJS)"

clean:
	rm -rf $(BUILD_DIR)
	rm -rf $(RUN_DIR)
	rm -f boot.img

-include $(DEPS)