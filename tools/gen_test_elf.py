#!/usr/bin/env python3
"""Gera test.elf minimo e valido (MIPS R5900, loop infinito).

Uso: python tools/gen_test_elf.py [saida.elf]
Padrao de saida: dist/test.elf

Nao desenha nada (sem grafico): serve para validar o caminho de boot
(ElfGuard -> LoadELF -> Resume -> loop da VM). Prova visual real exige
um sample ps2sdk com grafico (ex.: triangle/cube).
Formato: ELF32 LE, ET_EXEC, EM_MIPS, 1 PT_LOAD em 0x100000 com:
    loop: j loop
          nop
"""

import struct
import sys

ENTRY = 0x100000
SEG_OFF = 0x100

# j ENTRY (opcode 0x02 | ((ENTRY >> 2) & 0x3FFFFFF)), nop
CODE = struct.pack("<II", 0x08000000 | ((ENTRY >> 2) & 0x3FFFFFF), 0x00000000)

# ELF32 header LE: e_ident(16) type machine version entry phoff shoff flags
# ehsize phentsize phnum shentsize shnum shstrndx
ehdr = struct.pack(
    "<16sHHIIIIIHHHHHH",
    bytes([0x7F]) + b"ELF" + bytes([1, 1, 1, 0]) + bytes(8),  # e_ident
    2,      # ET_EXEC
    8,      # EM_MIPS
    1,      # version
    ENTRY,  # entry
    52,     # phoff
    0,      # shoff
    0,      # flags
    52,     # ehsize
    32,     # phentsize
    1,      # phnum
    0, 0, 0,
)

# Program header: type offset vaddr paddr filesz memsz flags align
phdr = struct.pack(
    "<IIIIIIII",
    1,          # PT_LOAD
    SEG_OFF,    # offset
    ENTRY,      # vaddr
    ENTRY,      # paddr
    len(CODE),  # filesz
    len(CODE),  # memsz
    5,          # R+X
    0x10,       # align
)

blob = ehdr + phdr
assert len(blob) == 84, len(blob)
blob += bytes(SEG_OFF - len(blob)) + CODE

# Autoverificacao (mesmas regras do ElfGuard.h)
assert blob[0:4] == b"\x7fELF"
assert struct.unpack("<H", blob[18:20])[0] == 8      # EM_MIPS
assert struct.unpack("<H", blob[16:18])[0] == 2      # ET_EXEC
assert struct.unpack("<I", blob[24:28])[0] == ENTRY  # entry

out = sys.argv[1] if len(sys.argv) > 1 else "dist/test.elf"
with open(out, "wb") as f:
    f.write(blob)
print(f"[OK] {out} ({len(blob)} bytes, entry=0x{ENTRY:X})")
