"""Linear disassembly of PSUC_Uncompressed.exe at a VA (for spots Ghidra has no function for).

usage: uv run --with capstone --with pefile tools/disasm.py 0x4FD990 [max_bytes]
"""
import sys

import capstone
import pefile

EXE = r"D:\Games\PSUClementine_22072025\PSUC_Uncompressed.exe"


def main():
    va = int(sys.argv[1], 16)
    size = int(sys.argv[2], 0) if len(sys.argv) > 2 else 0x400
    pe = pefile.PE(EXE, fast_load=True)
    rva = va - pe.OPTIONAL_HEADER.ImageBase
    data = pe.get_data(rva, size)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for insn in md.disasm(data, va):
        print(f"{insn.address:08X}  {insn.bytes.hex():<20} {insn.mnemonic} {insn.op_str}")
        if insn.mnemonic == "ret" or (insn.mnemonic == "int3"):
            break


if __name__ == "__main__":
    main()
