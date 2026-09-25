/*
 * OC OS — stage2: ядро загрузчика.
 *
 * Загружается stage1'ом на 0x8000, получает номер диска в DL.
 *
 * Что делает:
 *   1. Собирает карту памяти E820 (int 15h) в структуру oc_boot_info;
 *   2. Проверяет CPUID / поддержку Long Mode;
 *   3. Включает линию A20;
 *   4. Грузит ядро (kernel.bin) на KERNEL_STAGING;
 *   5. Подбирает и включает VBE-режим (линейный фреймбуфер);
 *   6. Строит таблицы страниц, входит в Protected Mode, копирует ядро
 *      на KERNEL_PHYS, входит в Long Mode и передаёт управление ядру
 *      (RDI = BOOTINFO_PHYS).
 *
 * Отладочные символы пишутся в порт 0xE9 (debugcon).
 */
#include "layout.h"
#include "boot.h"

#ifndef KERNEL_SECTORS
#define KERNEL_SECTORS 1
#endif
#ifndef KERNEL_BYTES
#define KERNEL_BYTES 512
#endif

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
    cld

    mov     %dl, boot_drive

    mov     $msg_stage2, %si
    call    puts16

/* ------------------------------------------------------------------ */
/* 1. Карта памяти E820                                                */
/* ------------------------------------------------------------------ */
do_e820:
    /* зачищаем заголовок bootinfo */
    mov     $BOOTINFO_PHYS, %di
    mov     $BI_OFF_E820, %cx
    xor     %al, %al
    rep     stosb

    movl    $BOOTINFO_MAGIC, BOOTINFO_PHYS + BI_OFF_MAGIC
    movl    $BOOTINFO_VERSION, BOOTINFO_PHYS + BI_OFF_VERSION
    movzbl  boot_drive, %eax
    movl    %eax, BOOTINFO_PHYS + BI_OFF_BOOT_DRIVE
    movl    $KERNEL_BYTES, BOOTINFO_PHYS + BI_OFF_KERNEL_SIZE

    movl    $0, e820_cont           /* EBX = 0 — начало списка */
    movw    $(BOOTINFO_PHYS + BI_OFF_E820), e820_ptr
    movw    $0, e820_count
e820_loop:
    xor     %ax, %ax
    mov     %ax, %es                /* BIOS пишет по ES:DI */
    mov     e820_ptr, %di
    movl    $0x534D4150, (%di)      /* "SMAP" — просим расширенные атрибуты */
    movl    $0xE820, %eax
    movl    $24, %ecx
    movl    $0x534D4150, %edx
    movl    e820_cont, %ebx
    int     $0x15
    jc      e820_done
    cmpl    $0x534D4150, %eax
    jne     e820_done
    cmpw    $20, %cx                /* старый BIOS: без атрибутов */
    jae     1f
    movw    $1, 20(%di)             /* attr = 1 (доступно) */
1:  addw    $BI_E820_ENTRY_SIZE, e820_ptr
    movl    %ebx, e820_cont
    incw    e820_count
    test    %ebx, %ebx              /* EBX = 0 после последней записи */
    jz      e820_done
    cmpw    $BI_E820_MAX, e820_count
    jb      e820_loop
e820_done:
    movzwl  e820_count, %eax
    movl    %eax, BOOTINFO_PHYS + BI_OFF_E820_COUNT
    movb    $'E', %al
    out     %al, $0xE9

/* ------------------------------------------------------------------ */
/* 2. Проверка CPU: CPUID и Long Mode                                  */
/* ------------------------------------------------------------------ */
check_cpu:
    pushfl
    popl    %eax
    movl    %eax, %ecx
    xorl    $(1 << 21), %eax        /* ID */
    pushl   %eax
    popfl
    pushfl
    popl    %eax
    pushl   %ecx
    popfl
    cmpl    %ecx, %eax
    je      no_long_mode            /* нет CPUID */
    movl    $0x80000000, %eax
    cpuid
    cmpl    $0x80000001, %eax
    jb      no_long_mode
    movl    $0x80000001, %eax
    cpuid
    testl   $(1 << 29), %edx        /* LM */
    jz      no_long_mode
    movb    $'C', %al
    out     %al, $0xE9

/* ------------------------------------------------------------------ */
/* 3. Линия A20                                                        */
/* ------------------------------------------------------------------ */
enable_a20:
    in      $0x92, %al
    orb     $2, %al
    andb    $0xFE, %al
    out     %al, $0x92
    call    check_a20
    test    %ax, %ax
    jnz     a20_ok
    /* запасной путь: контроллер клавиатуры */
    call    kbc_wait_in
    mov     $0xAD, %al
    out     %al, $0x64
    call    kbc_wait_in
    mov     $0xD0, %al
    out     %al, $0x64
    call    kbc_wait_out
    in      $0x60, %al
    push    %ax
    call    kbc_wait_in
    mov     $0xD1, %al
    out     %al, $0x64
    call    kbc_wait_in
    pop     %ax
    orb     $2, %al
    out     %al, $0x60
    call    kbc_wait_in
    mov     $0xAE, %al
    out     %al, $0x64
    call    check_a20
    test    %ax, %ax
    jz      a20_error
a20_ok:
    movb    $'A', %al
    out     %al, $0xE9

/* ------------------------------------------------------------------ */
/* 4. Загрузка ядра на KERNEL_STAGING                                  */
/* ------------------------------------------------------------------ */
load_kernel:
    /* пробуем EDD */
    mov     $0x55AA, %bx
    mov     $0x41, %ah
    mov     boot_drive, %dl
    int     $0x13
    jc      k_use_chs
    cmp     $0xAA55, %bx
    jne     k_use_chs
    test    $1, %cx
    jz      k_use_chs

    movl    $KERNEL_LBA, cur_lba
    movl    $KERNEL_SECTORS, left
    movw    $(KERNEL_STAGING >> 4), dest_seg
k_edd_loop:
    movl    left, %eax
    cmpl    $64, %eax               /* чанками по 32 KiB */
    jbe     1f
    movl    $64, %eax
1:  movw    %ax, dap_count
    movb    $0x10, dap_size
    movb    $0, dap_size + 1
    movw    $0, dap_off
    movw    dest_seg, %ax
    movw    %ax, dap_seg
    movl    cur_lba, %eax
    movl    %eax, dap_lba
    movl    $0, dap_lba + 4
    mov     $dap, %si
    mov     boot_drive, %dl
    mov     $0x42, %ah
    int     $0x13
    jc      disk_error
    movzwl  dap_count, %eax
    addl    %eax, cur_lba
    subl    %eax, left
    shll    $5, %eax                /* секторов -> параграфов (*32) */
    addw    %ax, dest_seg
    cmpl    $0, left
    jne     k_edd_loop
    jmp     kernel_loaded

    /* запасной путь: CHS по сектору */
k_use_chs:
    mov     boot_drive, %dl
    mov     $0x08, %ah
    int     $0x13
    jc      disk_error
    and     $0x3F, %cl
    mov     %cl, spt
    mov     %dh, %cl
    inc     %cl
    mov     %cl, heads

    movl    $KERNEL_LBA, cur_lba
    movl    $KERNEL_SECTORS, left
    movw    $(KERNEL_STAGING >> 4), dest_seg
    movw    $0, dest_off
k_chs_loop:
    movl    cur_lba, %eax
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
    andb    $0xC0, %al
    orb     %al, %cl                /* CL = сектор | цилиндр[9:8]<<6 */
    mov     $0x0201, %ax            /* AH=2, AL=1 сектор */
    mov     dest_seg, %es
    mov     dest_off, %bx
    mov     boot_drive, %dl
    int     $0x13
    jc      disk_error
    addw    $SECTOR_SIZE, dest_off
    jnc     1f
    addw    $0x1000, dest_seg       /* +64 KiB при переполнении смещения */
1:  incl    cur_lba
    decl    left
    jnz     k_chs_loop

kernel_loaded:
    movb    $'K', %al
    out     %al, $0xE9
    mov     $msg_kernel, %si
    call    puts16

/* ------------------------------------------------------------------ */
/* 5. VBE: подбор и включение графического режима                      */
/* ------------------------------------------------------------------ */
do_vbe:
    movw    $0x0003, %ax            /* текстовый режим 80x25 — база */
    int     $0x10

    /* VbeInfoBlock */
    xor     %ax, %ax
    mov     %ax, %es
    mov     $vbeinfo, %di
    mov     $128, %cx               /* 512 байт */
    xor     %eax, %eax
    rep     stosl
    movl    $0x32454256, vbeinfo    /* "VBE2" */
    mov     $vbeinfo, %di
    mov     $0x4F00, %ax
    int     $0x10
    cmp     $0x004F, %ax
    jne     no_vbe
    cmpl    $0x41534556, vbeinfo    /* "VESA" */
    jne     no_vbe

    /* список режимов: seg:off в vbeinfo+0x0E */
    movw    vbeinfo + 0x0E, %ax
    movw    %ax, list_off
    movw    vbeinfo + 0x10, %ax
    movw    %ax, mode_seg
    movw    $0, best_score
    movw    $0xFFFF, best_mode

mode_loop:
    /* читаем номер режима через lodsw (DS = mode_seg) */
    push    %ds
    mov     mode_seg, %ax
    mov     %ax, %ds
    mov     list_off, %si
    lodsw
    mov     %si, list_off
    pop     %ds
    mov     %ax, cur_mode
    cmp     $0xFFFF, %ax
    je      mode_done

    /* чистим ModeInfoBlock */
    xor     %ax, %ax
    mov     %ax, %es
    mov     $modeinfo, %di
    mov     $64, %cx                /* 256 байт */
    xor     %eax, %eax
    rep     stosl
    mov     cur_mode, %cx
    mov     $modeinfo, %di
    mov     $0x4F01, %ax
    int     $0x10
    cmp     $0x004F, %ax
    jne     mode_loop
    testw   $0x0080, modeinfo       /* линейный фреймбуфер */
    jz      mode_loop
    cmpb    $6, modeinfo + 0x1B     /* Direct Color */
    jne     mode_loop
    cmpb    $24, modeinfo + 0x19    /* bpp >= 24 */
    jb      mode_loop

    /* геометрия: 1024x768 (3 очка) или 800x600 (2 очка) */
    xor     %ax, %ax
    cmpw    $1024, modeinfo + 0x12
    jne     1f
    cmpw    $768, modeinfo + 0x14
    jne     1f
    mov     $3, %ax
1:  cmpw    $800, modeinfo + 0x12
    jne     2f
    cmpw    $600, modeinfo + 0x14
    jne     2f
    cmp     $3, %ax
    je      2f
    mov     $2, %ax
2:  test    %ax, %ax
    jz      mode_loop
    /* +2 за 32 bpp, +1 за 24 bpp */
    cmpb    $32, modeinfo + 0x19
    jne     3f
    add     $2, %ax
    jmp     4f
3:  add     $1, %ax
4:  cmp     best_score, %ax
    jbe     mode_loop
    mov     %ax, best_score
    mov     cur_mode, %ax
    mov     %ax, best_mode
    /* запоминаем параметры лучшего режима */
    movl    modeinfo + 0x28, %eax
    movl    %eax, fb_addr
    movzwl  modeinfo + 0x10, %eax
    movl    %eax, fb_pitch
    movzwl  modeinfo + 0x12, %eax
    movl    %eax, fb_width
    movzwl  modeinfo + 0x14, %eax
    movl    %eax, fb_height
    movzbl  modeinfo + 0x19, %eax
    movl    %eax, fb_bpp
    movb    modeinfo + 0x20, %al
    movb    %al, fb_rpos
    movb    modeinfo + 0x1F, %al
    movb    %al, fb_rsize
    movb    modeinfo + 0x22, %al
    movb    %al, fb_gpos
    movb    modeinfo + 0x21, %al
    movb    %al, fb_gsize
    movb    modeinfo + 0x24, %al
    movb    %al, fb_bpos
    movb    modeinfo + 0x23, %al
    movb    %al, fb_bsize
    movb    modeinfo + 0x26, %al
    movb    %al, fb_resvpos
    movb    modeinfo + 0x25, %al
    movb    %al, fb_resvsize
    jmp     mode_loop

mode_done:
    cmpw    $0xFFFF, best_mode
    je      no_vbe
    /* включаем режим (последнее действие через BIOS!) */
    xor     %ax, %ax
    mov     %ax, %es
    mov     best_mode, %bx
    or      $0x4000, %bx            /* линейный фреймбуфер */
    mov     $0x4F02, %ax
    int     $0x10
    cmp     $0x004F, %ax
    jne     no_vbe
    /* сохраняем в bootinfo */
    movl    fb_addr, %eax
    movl    %eax, BOOTINFO_PHYS + BI_OFF_FB_ADDR
    movl    $0, BOOTINFO_PHYS + BI_OFF_FB_ADDR + 4
    movl    fb_pitch, %eax
    movl    %eax, BOOTINFO_PHYS + BI_OFF_FB_PITCH
    movl    fb_width, %eax
    movl    %eax, BOOTINFO_PHYS + BI_OFF_FB_WIDTH
    movl    fb_height, %eax
    movl    %eax, BOOTINFO_PHYS + BI_OFF_FB_HEIGHT
    movl    fb_bpp, %eax
    movl    %eax, BOOTINFO_PHYS + BI_OFF_FB_BPP
    movb    fb_rpos, %al
    movb    %al, BOOTINFO_PHYS + BI_OFF_FB_RPOS
    movb    fb_rsize, %al
    movb    %al, BOOTINFO_PHYS + BI_OFF_FB_RSIZE
    movb    fb_gpos, %al
    movb    %al, BOOTINFO_PHYS + BI_OFF_FB_GPOS
    movb    fb_gsize, %al
    movb    %al, BOOTINFO_PHYS + BI_OFF_FB_GSIZE
    movb    fb_bpos, %al
    movb    %al, BOOTINFO_PHYS + BI_OFF_FB_BPOS
    movb    fb_bsize, %al
    movb    %al, BOOTINFO_PHYS + BI_OFF_FB_BSIZE
    movb    fb_resvpos, %al
    movb    %al, BOOTINFO_PHYS + BI_OFF_FB_RESV_POS
    movb    fb_resvsize, %al
    movb    %al, BOOTINFO_PHYS + BI_OFF_FB_RESV_SIZE
    movl    $1, BOOTINFO_PHYS + BI_OFF_FB_PRESENT
    jmp     vbe_done
no_vbe:
    movw    $0x0003, %ax            /* текстовый режим */
    int     $0x10
    movl    $0, BOOTINFO_PHYS + BI_OFF_FB_PRESENT
vbe_done:
    movb    $'V', %al
    out     %al, $0xE9

/* ------------------------------------------------------------------ */
/* 6. Protected Mode -> Long Mode (таблицы страниц строятся в pm_entry) */
/* ------------------------------------------------------------------ */
    cli
    lgdt    gdt_desc
    movl    %cr0, %eax
    orl     $1, %eax                /* PE */
    movl    %eax, %cr0
    ljmp    $GDT_CODE32, $pm_entry

    .code32
pm_entry:
    movw    $GDT_DATA32, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    %ax, %fs
    movw    %ax, %gs
    movl    $TEMP_STACK, %esp

    /* Таблицы страниц: идентичное отображение 0..4GiB страницами по 2MiB.
       ВАЖНО: записи таблиц 64-битные — пишем парами 32-битных слов
       (в 32-битном режиме нет movq). */
    movl    $BOOT_PML4, %edi
    xorl    %eax, %eax
    movl    $(6 * 512 * 2), %ecx
    cld
    rep     stosl                   /* чистим PML4+PDPT+4*PD = 24 KiB */

    movl    $(BOOT_PDPT | 3), %eax
    movl    %eax, BOOT_PML4
    movl    $0, BOOT_PML4 + 4

    movl    $BOOT_PD, %eax
    orl     $3, %eax
    movl    $BOOT_PDPT, %edi
    movl    $4, %ecx
1:  movl    %eax, (%edi)            /* PDPT[i] = PD[i] | P|RW, hi = 0 */
    movl    $0, 4(%edi)
    addl    $0x1000, %eax
    addl    $8, %edi
    loop    1b

    movl    $0x83, %eax             /* PD[j] = (j*2MiB) | P|RW|PS, hi = 0 */
    movl    $BOOT_PD, %edi
    movl    $2048, %ecx
2:  movl    %eax, (%edi)
    movl    $0, 4(%edi)
    addl    $8, %edi
    addl    $0x200000, %eax
    loop    2b
    movb    $'P', %al
    out     %al, $0xE9

    /* копируем ядро на KERNEL_PHYS */
    movl    $KERNEL_STAGING, %esi
    movl    $KERNEL_PHYS, %edi
    movl    $(KERNEL_SECTORS * SECTOR_SIZE / 4), %ecx
    cld
    rep     movsl

    /* PAE */
    movl    %cr4, %eax
    orl     $(1 << 5), %eax
    movl    %eax, %cr4
    movl    $BOOT_PML4, %eax
    movl    %eax, %cr3
    /* EFER.LME */
    movl    $0xC0000080, %ecx
    rdmsr
    orl     $(1 << 8), %eax
    wrmsr
    /* PG|PE */
    movl    %cr0, %eax
    orl     $0x80000001, %eax
    movl    %eax, %cr0
    ljmp    $GDT_CODE64, $lm_entry

    .code64
lm_entry:
    movw    $GDT_DATA32, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    %ax, %fs
    movw    %ax, %gs
    movq    $TEMP_STACK, %rsp
    xorq    %rbp, %rbp
    movb    $'L', %al
    out     %al, $0xE9
    movq    $BOOTINFO_PHYS, %rdi
    movq    $KERNEL_PHYS, %rax
    jmp     *%rax

/* ================================================================== */
/* Подпрограммы real mode                                              */
/* ================================================================== */
    .code16

/* puts16: вывод ASCIIZ DS:SI через BIOS-teletype и порт 0xE9 */
puts16:
    lodsb
    test    %al, %al
    jz      2f
    mov     $0x0E, %ah
    mov     $0x0007, %bx
    int     $0x10
    jmp     puts16
2:  ret

/* check_a20: возвращает AX=1, если A20 включена */
check_a20:
    push    %ds
    push    %es
    push    %si
    push    %di
    xor     %ax, %ax
    mov     %ax, %ds
    mov     $0xFFFF, %ax
    mov     %ax, %es
    mov     $0x0500, %si
    mov     $0x0510, %di
    movb    (%si), %al
    movb    %es:0(%di), %ah
    movb    $0x00, (%si)
    movb    $0xFF, %es:0(%di)
    cmpb    $0xFF, (%si)
    movb    %al, (%si)
    movb    %ah, %es:0(%di)
    mov     $0, %ax
    je      1f
    mov     $1, %ax
1:  pop     %di
    pop     %si
    pop     %es
    pop     %ds
    ret

kbc_wait_in:
    in      $0x64, %al
    test    $2, %al
    jnz     kbc_wait_in
    ret
kbc_wait_out:
    in      $0x64, %al
    test    $1, %al
    jz      kbc_wait_out
    ret

/* ---- Ошибки ---- */
no_long_mode:
    mov     $msg_nolm, %si
    jmp     fatal
a20_error:
    mov     $msg_noa20, %si
    jmp     fatal
disk_error:
    mov     $msg_disk, %si
fatal:
    call    puts16
1:  hlt
    jmp     1b

/* ================================================================== */
/* Данные                                                              */
/* ================================================================== */
    .section .data
boot_drive:
    .byte   0
spt:
    .byte   63
heads:
    .byte   16
    .align  2
e820_cont:
    .long   0
e820_ptr:
    .word   0
e820_count:
    .word   0
cur_lba:
    .long   0
left:
    .long   0
dest_seg:
    .word   0
dest_off:
    .word   0
mode_seg:
    .word   0
list_off:
    .word   0
cur_mode:
    .word   0
best_mode:
    .word   0
best_score:
    .word   0

    .align  2
dap:
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

    .align  2
fb_addr:
    .long   0
fb_pitch:
    .long   0
fb_width:
    .long   0
fb_height:
    .long   0
fb_bpp:
    .long   0
fb_rpos:
    .byte   0
fb_rsize:
    .byte   0
fb_gpos:
    .byte   0
fb_gsize:
    .byte   0
fb_bpos:
    .byte   0
fb_bsize:
    .byte   0
fb_resvpos:
    .byte   0
fb_resvsize:
    .byte   0

    .align  8
gdt:
    .quad   0x0000000000000000      /* 0x00: null */
    .quad   0x00CF9A000000FFFF      /* 0x08: код 32-bit, 4GiB */
    .quad   0x00CF92000000FFFF      /* 0x10: данные 32/64-bit */
    .quad   0x00AF9A000000FFFF      /* 0x18: код 64-bit */
gdt_end:
    .align  2
gdt_desc:
    .word   gdt_end - gdt - 1
    .long   gdt

    .align  4
vbeinfo:
    .space  512
modeinfo:
    .space  256

msg_stage2:
    .asciz  "OC stage2\r\n"
msg_kernel:
    .asciz  "kernel loaded\r\n"
msg_nolm:
    .asciz  "\r\nOC: CPU has no long mode!\r\n"
msg_noa20:
    .asciz  "\r\nOC: cannot enable A20!\r\n"
msg_disk:
    .asciz  "\r\nOC: disk read error!\r\n"
