"""
OC OS — тестовый стенд: мини-ПК на базе Unicorn Engine.

Эмулируется ровно то, что нужно нашей системе:
  * CPU x86 (16/32/64-bit, long mode, страничная адресация);
  * BIOS для загрузчика: int 0x10 (teletype, VBE), int 0x13 (диск),
    int 0x15 (E820);
  * порты: COM1, debugcon (0xE9), PIC 8259, PIT, PS/2 (клавиатура+мышь),
    VGA CRTC, 0x92, 0xCF9 (перезагрузка);
  * доставка аппаратных прерываний (IRQ0/1/12) в HLT-точках.

Особенности Unicorn: программные int и CPU-исключения приходят в
UC_HOOK_INTR (семантика IDT мы эмулируем сами), HLT останавливает
emu_start — стенд обрабатывает его снаружи.
"""
from __future__ import annotations

import struct
import subprocess

import sys, os
# unicorn/pillow ставятся в .pydeps (pip3 install --target=.pydeps ...):
# каталог ~/.local не переживает пересоздание песочницы, а этот — живёт в репо
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", ".pydeps"))
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UcError
from unicorn import UC_HOOK_INTR, UC_HOOK_INSN, UC_HOOK_MEM_UNMAPPED, UC_HOOK_CODE
from unicorn.x86_const import (
    UC_X86_INS_IN, UC_X86_INS_OUT,
    UC_X86_REG_EBP, UC_X86_REG_ESP,
    UC_X86_REG_ESI, UC_X86_REG_EDI,
    UC_X86_REG_EIP, UC_X86_REG_CS, UC_X86_REG_SS,
    UC_X86_REG_DS, UC_X86_REG_ES,
    UC_X86_REG_EFLAGS, UC_X86_REG_CR0,
    UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDX,
)

RAM_SIZE = 128 * 1024 * 1024
FB_PHYS = 0xFD000000
FB_SIZE = 8 * 1024 * 1024

# ASCII -> скан-коды (set 1, раскладка EN)
ASCII_SC = {
    'a': 0x1E, 'b': 0x30, 'c': 0x2E, 'd': 0x20, 'e': 0x12, 'f': 0x21,
    'g': 0x22, 'h': 0x23, 'i': 0x17, 'j': 0x24, 'k': 0x25, 'l': 0x26,
    'm': 0x32, 'n': 0x31, 'o': 0x18, 'p': 0x19, 'q': 0x10, 'r': 0x13,
    's': 0x1F, 't': 0x14, 'u': 0x16, 'v': 0x2F, 'w': 0x11, 'x': 0x2D,
    'y': 0x15, 'z': 0x2C,
    '1': 0x02, '2': 0x03, '3': 0x04, '4': 0x05, '5': 0x06, '6': 0x07,
    '7': 0x08, '8': 0x09, '9': 0x0A, '0': 0x0B,
    '-': 0x0C, '=': 0x0D, '[': 0x1A, ']': 0x1B, ';': 0x27, "'": 0x28,
    '`': 0x29, '\\': 0x2B, ',': 0x33, '.': 0x34, '/': 0x35, ' ': 0x39,
}
ASCII_SC_SHIFT = {
    '!': 0x02, '@': 0x03, '#': 0x04, '$': 0x05, '%': 0x06, '^': 0x07,
    '&': 0x08, '*': 0x09, '(': 0x0A, ')': 0x0B, '_': 0x0C, '+': 0x0D,
    '{': 0x1A, '}': 0x1B, ':': 0x27, '"': 0x28, '~': 0x29, '|': 0x2B,
    '<': 0x33, '>': 0x34, '?': 0x35,
}

KEY_SC = {
    'enter': 0x1C, 'backspace': 0x0E, 'tab': 0x0F, 'esc': 0x01,
    'up': 0xE048, 'down': 0xE050, 'left': 0xE04B, 'right': 0xE04D,
}


class PC:
    def __init__(self, image_path: str, kernel_elf: str | None = None,
                 verbose: bool = False):
        with open(image_path, "rb") as f:
            self.image = f.read()
        self.verbose = verbose
        self.symbols = self._load_symbols(kernel_elf)

        self.serial_log = []
        self.debug_log = []
        self.boot_log = []
        self.screen_text = []
        self.status = None
        self.idle_hits = 0

        self.kbc_out = []
        self.kbc_expect = None
        self.kbd_fifo = []
        self.mouse_stream = []
        self.pending_irq = []
        self.pic_cmd = [0, 0]
        self.pit_div = [0, 0, 0]
        self.pit_latch_lo = True
        self.vbe_current_mode = 3
        self.crtc = {}
        self.reboot_requested = False

        self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        self.uc.mem_map(0, RAM_SIZE)
        self.uc.mem_map(FB_PHYS, FB_SIZE)

        self.uc.hook_add(UC_HOOK_INTR, self._hook_intr)
        self.uc.hook_add(UC_HOOK_INSN, self._hook_in, None, 1, 0,
                         UC_X86_INS_IN)
        self.uc.hook_add(UC_HOOK_INSN, self._hook_out, None, 1, 0,
                         UC_X86_INS_OUT)
        self.uc.hook_add(UC_HOOK_MEM_UNMAPPED, self._hook_unmapped)

        self.uc.mem_write(0x7C00, self.image[:512])

        self.uc.reg_write(UC_X86_REG_CS, 0)
        self.uc.reg_write(UC_X86_REG_SS, 0)
        self.uc.reg_write(UC_X86_REG_EIP, 0x7C00)
        self.uc.reg_write(UC_X86_REG_ESP, 0x7C00)

    # ------------------------------------------------------------------
    @staticmethod
    def _load_symbols(kernel_elf):
        syms = {}
        if not kernel_elf:
            return syms
        try:
            out = subprocess.check_output(["nm", "-n", kernel_elf], text=True)
        except Exception:
            return syms
        for line in out.splitlines():
            parts = line.split()
            if len(parts) == 3:
                addr, kind, name = parts
                try:
                    syms[name] = int(addr, 16)
                except ValueError:
                    pass
        return syms

    def read(self, addr, size):
        return self.uc.mem_read(addr, size)

    def write(self, addr, data):
        self.uc.mem_write(addr, data)

    def reg(self, name):
        regmap = {
            "rax": UC_X86_REG_EAX, "rbx": UC_X86_REG_EBX,
            "rcx": UC_X86_REG_ECX, "rdx": UC_X86_REG_EDX,
            "rsi": UC_X86_REG_ESI, "rdi": UC_X86_REG_EDI,
            "rbp": UC_X86_REG_EBP, "rsp": UC_X86_REG_ESP,
            "rip": UC_X86_REG_EIP, "rflags": UC_X86_REG_EFLAGS,
            "cr0": UC_X86_REG_CR0, "cs": UC_X86_REG_CS,
            "ds": UC_X86_REG_DS, "es": UC_X86_REG_ES,
            "eax": UC_X86_REG_EAX, "ebx": UC_X86_REG_EBX,
            "ecx": UC_X86_REG_ECX, "edx": UC_X86_REG_EDX,
        }
        return self.uc.reg_read(regmap[name])

    # ------------------------------------------------------------------
    def _hook_unmapped(self, uc, access, address, size, value, user):
        self.status = (f"unmapped access {access} addr=0x{address:x} "
                       f"size={size} rip=0x{uc.reg_read(UC_X86_REG_EIP):x}")
        return False

    def _hook_in(self, uc, port, size, user_data):
        return self._port_in(port, size)

    def _hook_out(self, uc, port, size, value, user_data):
        self._port_out(port, size, value)

    def _hook_intr(self, uc, intno, user_data):
        """Сюда попадают и int n, и CPU-исключения (Unicorn отдаёт их вектор)."""
        cr0 = uc.reg_read(UC_X86_REG_CR0)
        rip = uc.reg_read(UC_X86_REG_EIP)     # уже «адрес возврата»
        if (cr0 & 1) == 0:
            # real mode: BIOS-сервисы
            if 0x10 <= intno <= 0x1A:
                self._bios(intno)
            else:
                self.status = f"exception 0x{intno:x} in real mode (rip=0x{rip:x})"
                uc.emu_stop()
        else:
            if intno in (6, 13):
                # #GP/#UD на привилегированных инструкциях в «пользовательском»
                # CS: на железе ядерный код работает в ring0 и #GP не получает;
                # в UC_MODE_16 CS остаётся 0x23 — пропускаем hlt/sti/cli как NOP.
                # ВАЖНО: для исключений EIP может указывать И за командой, и на
                # неё — проверяем оба байта (0xF4=hlt, 0xFA=sti, 0xFB=cli).
                cs = uc.reg_read(UC_X86_REG_CS)
                if (cs & 3) == 3:
                    hit = None
                    for off in (0, -1):
                        try:
                            bb = self.read(rip + off, 1)[0]
                        except Exception:
                            continue
                        if bb in (0xF4, 0xFA, 0xFB):
                            hit = (rip + off, bb)
                            break
                    if hit:
                        pos, bb = hit
                        uc.reg_write(UC_X86_REG_EIP, pos + 1)
                        if bb == 0xF4:
                            # hlt из syscall-контекста программы: доставить
                            # IRQ (как обычный hlt-путь), иначе sleep-цикл
                            # программ не увидит ни таймера, ни клавиатуры
                            self._refill_irqs()
                            irq = (self.pending_irq.pop(0)
                                   if self.pending_irq else 0)
                            self._inject(32 + irq, retaddr=pos + 1)
                        return
            self._inject(intno, retaddr=rip)

    # ------------------------------------------------------------------
    def _refill_irqs(self):
        if self.kbd_fifo and 1 not in self.pending_irq:
            self.pending_irq.append(1)
        if self.mouse_stream and 12 not in self.pending_irq:
            self.pending_irq.append(12)

    def _inject(self, vector, retaddr=None):
        """Доставка вектора так, как это делает CPU через IDT."""
        uc = self.uc
        rip = uc.reg_read(UC_X86_REG_EIP)
        cs = uc.reg_read(UC_X86_REG_CS)
        ss = uc.reg_read(UC_X86_REG_SS)
        rsp = uc.reg_read(UC_X86_REG_ESP)
        rflags = uc.reg_read(UC_X86_REG_EFLAGS)
        if retaddr is None:
            retaddr = rip

        if vector == 0x80:
            handler = self.symbols.get("isr128")
        else:
            handler = self.symbols.get(f"isr{vector}")
        if handler is None and 32 <= vector < 48:
            handler = self.symbols.get(f"irq{vector - 32}")
        if handler is None:
            self.status = f"нет стаба для вектора 0x{vector:x} (rip=0x{rip:x})"
            uc.emu_stop()
            return

        # кадр iretq: по младшему адресу RIP, затем CS, RFLAGS, RSP, SS
        frame = struct.pack("<QQQQQ", retaddr, cs, rflags, rsp, ss)
        rsp -= len(frame)
        uc.mem_write(rsp, frame)
        uc.reg_write(UC_X86_REG_ESP, rsp)
        uc.reg_write(UC_X86_REG_EIP, handler)
        uc.reg_write(UC_X86_REG_EFLAGS, rflags & ~0x200)

    def _handle_hlt(self):
        """HLT остановил emu_start: таймер/IRQ/простой.
        Возвращает: 'hlt' (остановились на hlt), 'busy' (конец квоты чанка),
        'stop' (аварийный стоп)."""
        rip = self.reg("rip")
        b = self.read(rip - 1, 1)[0]
        if b != 0xF4:
            # Конец кванта: прерывания доставляем ТОЛЬКО из кода программы
            # (0x2000000..) — «спокойная» точка; в обработчиках и ядре
            # впрыск посреди кадра стека недопустим (это вам не реальный CPU).
            self._refill_irqs()
            if not (0x2000000 <= rip < 0x2200000):
                return "busy"
            irq = self.pending_irq.pop(0) if self.pending_irq else 0
            self._inject(32 + irq, retaddr=rip)
            return "busy"
        eflags = self.reg("rflags")
        if not (eflags & 0x200):                  # IF=0
            # ожидающие клавиатура/мышь доставляем даже при IF=0:
            # ocp_exit возвращается из обработчика без iretq (IF остаётся 0)
            self._refill_irqs()
            pend = [i for i in self.pending_irq if i in (1, 12)]
            if pend:
                self.pending_irq.remove(pend[0])
                self.idle_hits = 0
                self._inject(32 + pend[0], retaddr=rip)
                return "hlt"
            self.idle_hits += 1
            if self.idle_hits > 3:
                self.status = self.status or "halted (cli;hlt)"
                return "stop"
            return "hlt"
        self.idle_hits = 0
        self._refill_irqs()
        # на каждом HLT тикает PIT; клавиатура/мышь — в приоритете
        if self.pending_irq:
            irq = self.pending_irq.pop(0)
        else:
            irq = 0                               # IRQ0: таймер
        self._inject(32 + irq, retaddr=rip)
        return "hlt"

    # ------------------------------------------------------------------
    def _port_in(self, port, size):
        if 0x3F8 <= port <= 0x3FF:
            return 0x20 if port == 0x3F8 + 5 else 0
        if port == 0x64:
            self._refill_irqs()
            return 0x01 if (self.kbc_out or self.kbd_fifo or
                            self.mouse_stream) else 0x00
        if port == 0x60:
            for src in (self.kbc_out, self.kbd_fifo, self.mouse_stream):
                if src:
                    return src.pop(0)
            return 0
        if port in (0x40, 0x41, 0x42):
            if self.pit_latch_lo:
                self.pit_latch_lo = False
                return 0x34
            self.pit_latch_lo = True
            return 0x12
        if port in (0x3D4, 0x3D5):
            return self.crtc.get(port, 0)
        if port == 0x3DA:
            return 0x00
        if port == 0x92:
            return 0x02
        return 0xFF

    def _port_out(self, port, size, value):
        # Запись в дисковые контроллеры (ATA/IDE/флоппи/DMA) — под запретом:
        # ОС не должна иметь права трогать диски пользователя. Копим всё.
        if (port in (0x3F6, 0x3F7, 0x376) or
                0x1F0 <= port <= 0x1F7 or 0x170 <= port <= 0x177 or
                0x3F0 <= port <= 0x3F5 or port < 0x10 or 0xC0 <= port <= 0xDF):
            if not hasattr(self, "disk_writes"):
                self.disk_writes = []
            self.disk_writes.append((port, value))
        if 0x3F8 <= port <= 0x3FF:
            if port == 0x3F8:
                self.serial_log.append(chr(value & 0xFF))
            return
        if port == 0xE9:
            self.debug_log.append(chr(value & 0xFF))
            return
        if port in (0x20, 0xA0):
            self.pic_cmd[0 if port == 0x20 else 1] = value
            return
        if port in (0x21, 0xA1):
            return
        if port == 0x43 or port in (0x40, 0x41, 0x42):
            return
        if port == 0x64:
            if value == 0x20:
                self.kbc_out.append(0x47)
            elif value in (0x60, 0xD1):
                self.kbc_expect = value
            elif value == 0xD0:
                self.kbc_out.append(0x02)
            elif value == 0xD4:
                self.kbc_expect = 0xD4
            elif value == 0xAA:
                self.kbc_out.append(0x55)
            return
        if port == 0x60:
            if self.kbc_expect == 0xD4:
                self.kbc_expect = None
                self.kbc_out.append(0xFA)         # ACK мыши
                self._refill_irqs()
            else:
                self.kbc_expect = None
            return
        if port in (0x3D4, 0x3D5):
            self.crtc[port] = value
            return
        if port == 0xCF9:
            if value & 0x04:
                self.reboot_requested = True
                self.status = "reboot"
                self.uc.emu_stop()
            return

    # ------------------------------------------------------------------
    def _reg16(self, name):
        regmap = {"ax": "eax", "bx": "ebx", "cx": "ecx", "dx": "edx",
                  "si": "rsi", "di": "rdi", "ds": "ds", "es": "es"}
        return self.reg(regmap[name]) & 0xFFFF

    def _set16(self, name, val):
        regmap = {"ax": UC_X86_REG_EAX, "bx": UC_X86_REG_EBX,
                  "cx": UC_X86_REG_ECX, "dx": UC_X86_REG_EDX,
                  "si": UC_X86_REG_ESI, "di": UC_X86_REG_EDI}
        cur = self.uc.reg_read(regmap[name])
        self.uc.reg_write(regmap[name], (cur & ~0xFFFF) | (val & 0xFFFF))

    def _set_flags(self, carry):
        fl = self.uc.reg_read(UC_X86_REG_EFLAGS)
        fl = (fl | 1) if carry else (fl & ~1)
        self.uc.reg_write(UC_X86_REG_EFLAGS, fl)

    @staticmethod
    def _phys(seg, off):
        return (seg << 4) + off

    def _bios(self, intno):
        ah = (self._reg16("ax") >> 8) & 0xFF
        al = self._reg16("ax") & 0xFF
        if self.verbose:
            self.boot_log.append(f"<int{intno:02x}h ah={ah:02x}>")
        if intno == 0x10:
            self._bios_video(ah, al)
        elif intno == 0x13:
            self._bios_disk(ah)
        elif intno == 0x15:
            self._bios_mem()
        elif intno == 0x12:
            self._set16("ax", 640)
            self._set_flags(False)
        elif intno == 0x11:
            self._set16("ax", 0x0021)
            self._set_flags(False)
        elif intno == 0x16:
            if ah == 0x01:
                self._set_flags(True)
            else:
                self._set16("ax", 0)
        else:
            self._set_flags(False)

    def _bios_video(self, ah, al):
        if ah == 0x0E:
            if al == 0x0A:
                self.screen_text.append("\n")
            elif al != 0x0D:
                self.screen_text.append(chr(al))
        elif ah == 0x00:
            self.vbe_current_mode = al & 0x7F
            self.screen_text.append(f"\n<video mode {al & 0x7F}>\n")
        elif ah == 0x0F:
            self._set16("ax", 0x5003)
        elif ah == 0x4F:
            self._bios_vbe(al)
        self._set_flags(False)

    VBE_MODES = {
        0x115: (800, 600, 24, 2400),
        0x116: (800, 600, 32, 3200),
        0x118: (1024, 768, 24, 3072),
        0x119: (1024, 768, 32, 4096),
    }

    def _bios_vbe(self, al):
        di = self._reg16("di")
        es = self._reg16("es")
        dst = self._phys(es, di)
        if al == 0x00:
            buf = bytearray(512)
            struct.pack_into("<I", buf, 0, 0x41534556)      # "VESA"
            struct.pack_into("<H", buf, 4, 0x0200)
            struct.pack_into("<I", buf, 0x0E, 0x0000F500)   # seg:off списка
            struct.pack_into("<H", buf, 0x12, 64)
            self.uc.mem_write(dst, bytes(buf))
            lst = b""
            for m in (0x118, 0x119, 0x115, 0x116):
                lst += struct.pack("<H", m)
            lst += struct.pack("<H", 0xFFFF)
            self.uc.mem_write(0xF500, lst)
            self._set16("ax", 0x004F)
        elif al == 0x01:
            mode = self._reg16("cx")
            if mode not in self.VBE_MODES:
                self._set16("ax", 0x014F)
                return
            w, h, bpp, pitch = self.VBE_MODES[mode]
            buf = bytearray(256)
            struct.pack_into("<H", buf, 0x00, 0x0081)   # LFB | supported
            struct.pack_into("<H", buf, 0x10, pitch)
            struct.pack_into("<H", buf, 0x12, w)
            struct.pack_into("<H", buf, 0x14, h)
            buf[0x19] = bpp
            buf[0x1B] = 6
            buf[0x1F], buf[0x20] = 8, 16
            buf[0x21], buf[0x22] = 8, 8
            buf[0x23], buf[0x24] = 8, 0
            buf[0x25], buf[0x26] = 8, 24
            struct.pack_into("<I", buf, 0x28, FB_PHYS)
            self.uc.mem_write(dst, bytes(buf))
            self._set16("ax", 0x004F)
        elif al == 0x02:
            mode = self._reg16("bx") & 0x3FFF
            if mode in self.VBE_MODES:
                self.vbe_current_mode = mode
                w, h, bpp, _ = self.VBE_MODES[mode]
                self.screen_text.append(
                    f"\n<vbe mode 0x{mode:x} {w}x{h}x{bpp}>\n")
                self._set16("ax", 0x004F)
            else:
                self._set16("ax", 0x014F)
        else:
            self._set16("ax", 0x014F)

    def _bios_disk(self, ah):
        if ah == 0x41:
            if self._reg16("bx") == 0x55AA:
                self._set16("bx", 0xAA55)
                self._set16("cx", 0x0001)
                self._set_flags(False)
            else:
                self._set_flags(True)
        elif ah == 0x42:
            si = self._reg16("si")
            ds = self._reg16("ds")
            dap = bytes(self.uc.mem_read(self._phys(ds, si), 16))
            count = struct.unpack_from("<H", dap, 2)[0]
            off = struct.unpack_from("<H", dap, 4)[0]
            seg = struct.unpack_from("<H", dap, 6)[0]
            lba = struct.unpack_from("<Q", dap, 8)[0]
            data = self.image[lba * 512:(lba + count) * 512]
            if len(data) < count * 512:
                data += b"\x00" * (count * 512 - len(data))
            self.uc.mem_write(self._phys(seg, off), data)
            self._set16("ax", 0x0000)
            self._set_flags(False)
        elif ah == 0x02:
            count = self._reg16("ax") & 0xFF
            cx = self._reg16("cx")
            dh = (self._reg16("dx") >> 8) & 0xFF
            sec = cx & 0x3F
            cyl = ((cx >> 8) & 0xFF) | ((cx & 0xC0) << 2)
            lba = (cyl * 16 + dh) * 63 + (sec - 1)
            bx = self._reg16("bx")
            es = self._reg16("es")
            data = self.image[lba * 512:(lba + max(count, 1)) * 512]
            want = max(count, 1) * 512
            if len(data) < want:
                data += b"\x00" * (want - len(data))
            self.uc.mem_write(self._phys(es, bx), data)
            self._set16("ax", count)
            self._set_flags(False)
        elif ah == 0x08:
            self._set16("ax", 0)
            self._set16("cx", 63 | (1 << 8))
            self._set16("dx", (15 << 8) | 1)
            self._set_flags(False)
        else:
            self._set_flags(True)

    def _bios_mem(self):
        eax = self.uc.reg_read(UC_X86_REG_EAX) & 0xFFFFFFFF
        if eax != 0xE820:
            self._set_flags(True)
            return
        ebx = self.uc.reg_read(UC_X86_REG_EBX) & 0xFFFFFFFF
        di = self._reg16("di")
        es = self._reg16("es")
        entries = [
            (0x00000000, 0x0009FC00, 1),
            (0x0009FC00, 0x00000400, 2),
            (0x000A0000, 0x00060000, 2),
            (0x00100000, 0x07E00000 - 0x00100000, 1),
            (FB_PHYS, 4 * 1024 * 1024, 2),
        ]
        if ebx >= len(entries):
            self._set_flags(True)
            return
        base, ln, typ = entries[ebx]
        self.uc.mem_write(self._phys(es, di),
                          struct.pack("<QQLL", base, ln, typ, 1))
        self.uc.reg_write(UC_X86_REG_EAX, 0x534D4150)
        self.uc.reg_write(UC_X86_REG_ECX, 24)
        self.uc.reg_write(UC_X86_REG_EBX,
                          ebx + 1 if ebx + 1 < len(entries) else 0)
        self._set_flags(False)

    # ------------------------------------------------------------------
    def serial(self):
        # ядро шлёт в COM1 UTF-8; собираем байты и декодируем,
        # чтобы проверки могли искать обычные русские строки
        raw = "".join(self.serial_log).encode("latin-1", "replace")
        return raw.decode("utf-8", "replace")

    def debug(self):
        return "".join(self.debug_log)

    def boot_text(self):
        return "".join(self.screen_text)

    def queue_scancode(self, code):
        if code > 0xFF:
            self.kbd_fifo.append(0xE0)
            self.kbd_fifo.append(code & 0xFF)
        else:
            self.kbd_fifo.append(code)
        self._refill_irqs()

    def _pump(self, limit=26):
        """Прокрутить emu, пока скан-коды уйдут в ядро (темп как у человека)."""
        for _ in range(limit):
            if not self.kbd_fifo and not self.mouse_stream:
                return
            if self.status:
                return
            try:
                self._emu_start_at(self.reg("rip"), 400_000)
            except Exception:
                return
            self._handle_hlt()

    def type_text(self, s):
        for ch in s:
            if ch == "\n":
                self.queue_scancode(KEY_SC["enter"])
                self.queue_scancode(KEY_SC["enter"] | 0x80)
            elif ch == "\b":
                self.queue_scancode(KEY_SC["backspace"])
                self.queue_scancode(KEY_SC["backspace"] | 0x80)
            elif ch in ASCII_SC:
                self.queue_scancode(ASCII_SC[ch])
                self.queue_scancode(ASCII_SC[ch] | 0x80)
            elif ch in ASCII_SC_SHIFT:
                self.queue_scancode(0x2A)
                self.queue_scancode(ASCII_SC_SHIFT[ch])
                self.queue_scancode(ASCII_SC_SHIFT[ch] | 0x80)
                self.queue_scancode(0x2A | 0x80)
            else:
                raise ValueError(f"нет скан-кода для {ch!r}")
            self._pump()

    def send_mouse_packet(self, dx, dy, buttons=0):
        b0 = 0x08 | (buttons & 7)
        if dx < 0:
            b0 |= 0x10
        if dy < 0:
            b0 |= 0x20
        self.mouse_stream += [b0, dx & 0xFF, dy & 0xFF]
        self._refill_irqs()

    def tick(self, n=1):
        for _ in range(n):
            self.pending_irq.append(0)

    def _emu_start_at(self, target, count):
        """Запуск/продолжение с произвольного RIP (в т.ч. ≥ 0x100000).

        Грабли Unicorn: в UC_MODE_16 параметр begin пишется в16-битный IP —
        фактическая посадка = (begin & 0xFFFF) - 0x180, поэтому рестарт прямо
        на адрес ядра невозможен. Рестартуем через трамплин в низкой памяти
        и перекидываем EIP из хука (приём, уже проверенный в _inject).
        """
        if getattr(self, "_tramp_hook", None) is None:
            self.TRAMP = 0x500
            # вся зона 0x500..0x7FF заполнена 'jmp $' — посадка emu_start
            # зависит от состояния движка (то begin-0x180, то begin как есть),
            # а оба варианта для begin=0x680 попадают сюда
            self.write(self.TRAMP, b"\xeb\xfe" * 0x180)
            self._tramp_hook = self.uc.hook_add(
                UC_HOOK_CODE, self._on_trampoline,
                begin=self.TRAMP, end=self.TRAMP + 0x300)
        self._tramp_target = target
        try:
            self.uc.emu_start(self.TRAMP + 0x180, 0xFFFFFFFF,
                              timeout=0, count=count + 8)
        finally:
            self._tramp_target = None

    def _on_trampoline(self, uc, addr, size, user):
        if self._tramp_target is not None:
            uc.reg_write(UC_X86_REG_EIP, self._tramp_target)

    def run(self, max_insns=80_000_000):
        """Выполнение до останова. status=None => штатный cli;hlt."""
        if self.status:
            return self.status       # останов уже случился (напр. перезагрузка)
        budget = max_insns
        self.status = None
        self.idle_hits = 0
        chunk = 4_000_000
        while budget > 0:
            n = min(chunk, budget)
            try:
                self._emu_start_at(self.reg("rip"), n)
            except UcError as e:
                self.status = f"{e} (rip=0x{self.reg('rip'):x})"
                return self.status
            if self.status:
                return self.status
            # HLT или конец бюджета чанка
            h = self._handle_hlt()
            if h == "stop":
                return self.status
            # холостые пробуждения по hlt почти бесплатны
            budget -= 2_000 if h == "hlt" else n
        self.status = self.status or "бюджет инструкций исчерпан"
        return self.status

    def run_until_serial(self, text, max_insns=80_000_000):
        budget = max_insns
        self.status = None
        self.idle_hits = 0
        chunk = 4_000_000
        while budget > 0:
            n = min(chunk, budget)
            try:
                self._emu_start_at(self.reg("rip"), n)
            except UcError as e:
                self.status = f"{e} (rip=0x{self.reg('rip'):x})"
                return False
            if text in self.serial():
                return True
            if self.status:
                return False
            h = self._handle_hlt()
            if h == "stop":
                return False
            budget -= 2_000 if h == "hlt" else n
        return text in self.serial()

    def run_until_boot_text(self, text, max_insns=80_000_000):
        """Как run_until_serial, но ищет строку в BIOS-teletype выводе
        (туда пишет stage2 через int 10h, а не в serial)."""
        budget = max_insns
        self.status = None
        self.idle_hits = 0
        chunk = 4_000_000
        while budget > 0:
            n = min(chunk, budget)
            try:
                self._emu_start_at(self.reg("rip"), n)
            except UcError as e:
                self.status = f"{e} (rip=0x{self.reg('rip'):x})"
                return False
            if text in self.boot_text():
                return True
            if self.status:
                return False
            h = self._handle_hlt()
            if h == "stop":
                return False
            budget -= 2_000 if h == "hlt" else n
        return text in self.boot_text()
