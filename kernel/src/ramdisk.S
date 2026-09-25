/* OC — встроенный ramdisk (образ OCFS, собирается scripts/packfs.py). */
    .section .rodata
    .global ramdisk_start
    .global ramdisk_end
ramdisk_start:
    .incbin "build/ramdisk.bin"
ramdisk_end:
