/*
 * OC OS — stage1: загрузочный сектор (MBR), 512 байт.
 *
 * Задача: найти stage2 на диске, загрузить его на 0x8000 и передать
 * управление, сохранив номер загрузочного диска в DL.
 *
 * Поддерживает BIOS EDD (int 13h ah=42h) и запасной CHS-режим.
 */
#include "layout.h"

    .code16
    .section .text
    .global _start

_start:
    cli
    xor     %ax, %ax
    mov     %ax, %ds
    mov     %ax, %es
    mov     %ax, %ss
    mov     $BOOT_STACK, %sp
    sti

    mov     %dl, boot_drive

    /* ---- Проверяем поддержку EDD (int 13h ah=41h) ---- */
    mov     $0x55AA, %bx
    mov     $0x41, %ah
    mov     boot_drive, %dl
    int     $0x13
    jc      use_chs
    cmp     $0xAA55, %bx
    jne     use_chs
    test    $1, %cx                 /* бит 0: поддержка пакетного API */
    jz      use_chs

    /* ---- EDD: одним пакетом читаем весь stage2 ---- */
    movb    $0x10, dap_size
    movb    $0, dap_size + 1
    movw    $STAGE2_SECTORS, dap_count
    movw    $STAGE2_ENTRY, dap_off
    movw    $0, dap_seg
    movl    $STAGE2_LBA, dap_lba
    movl    $0, dap_lba + 4
    mov     $dap, %si
    mov     boot_drive, %dl
    mov     $0x42, %ah
    int     $0x13
    jc      disk_error
    jmp     load_done

    /* ---- Запасной вариант: CHS по одному сектору ---- */
use_chs:
    mov     boot_drive, %dl
    mov     $0x08, %ah
    int     $0x13
    jc      disk_error
    and     $0x3F, %cl              /* CL[5:0] = секторов на дорожку */
    mov     %cl, spt
    mov     %dh, %cl
    inc     %cl                     /* DH+1 = головок */
    mov     %cl, heads

    movw    $STAGE2_LBA, lba
    movw    $STAGE2_SECTORS, count
    movw    $STAGE2_ENTRY, %bx
chs_loop:
    /* LBA -> CHS */
    movzwl  lba, %eax
    xor     %edx, %edx
    movzbl  spt, %ecx
    div     %ecx                    /* EAX=LBA/spt, EDX=сектор-1 */
    mov     %edx, %ecx
    inc     %cx                     /* ECX = сектор */
    xor     %edx, %edx
    movzbl  heads, %esi
    div     %esi                    /* EAX=цилиндр, EDX=головка */
    mov     %dl, %dh                /* DH = головка */
    mov     %al, %ch                /* CH = цилиндр[7:0] */
    shrl    $2, %eax
    andb    $0xC0, %al              /* цилиндр[9:8] -> биты 7:6 */
    orb     %al, %cl                /* CL = сектор | цилиндр[9:8]<<6 */
    mov     $0x0201, %ax            /* AH=2: чтение, AL=1 сектор */
    mov     boot_drive, %dl
    int     $0x13
    jc      disk_error
    addw    $SECTOR_SIZE, %bx
    incw    lba
    decw    count
    jnz     chs_loop

load_done:
    mov     boot_drive, %dl         /* BIOS-конвенция: DL = диск */
    ljmp    $0x0000, $STAGE2_ENTRY

disk_error:
    mov     $msg_err, %si
    call    print16
halt:
    hlt
    jmp     halt

/* ---- Вывод строки ASCIIZ на экран, DS:SI ---- */
print16:
    lodsb
    test    %al, %al
    jz      2f
    mov     $0x0E, %ah
    mov     $0x0007, %bx
    int     $0x10
    jmp     print16
2:  ret

/* ---- Данные ---- */
boot_drive:
    .byte   0
spt:
    .byte   63
heads:
    .byte   16
lba:
    .word   0
count:
    .word   0

    .align  4
dap:                            /* Disk Address Packet (int 13h ah=42h) */
dap_size:
    .byte   0x10
    .byte   0
dap_count:
    .word   0
dap_off:
    .word   0
dap_seg:
    .word   0
dap_lba:
    .quad   0

msg_err:
    .asciz  "OC!disk"

/* ---- MBR: таблица разделов и сигнатура ---- */
    .org    446
part_entry0:                    /* раздел 0xEF (EFI System Partition) */
    .byte   0x80                /* флаг загрузки */
    .byte   0xFE, 0xFF, 0xFF    /* начало CHS (фиктивное) */
    .byte   PART_TYPE           /* тип: 0xEF */
    .byte   0xFE, 0xFF, 0xFF    /* конец CHS (фиктивное) */
    .long   PART_LBA
    .long   PART_SECTORS
    .org    510
    .word   0xAA55
