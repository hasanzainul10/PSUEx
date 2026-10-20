# List instructions in the code section that use [reg+DISP] (optionally only writes / a mnemonic filter).
import sys, pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
pe = pefile.PE(sys.argv[1], fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
disp = int(sys.argv[2], 16)
only_write = len(sys.argv) > 3 and sys.argv[3] == 'w'
text = next(s for s in pe.sections if s.VirtualAddress <= 0x127B60 < s.VirtualAddress + s.Misc_VirtualSize)
data = text.get_data()
va0 = base + text.VirtualAddress
needle = disp.to_bytes(4, 'little')
md = Cs(CS_ARCH_X86, CS_MODE_32)
seen = set()
i = data.find(needle)
tag = '+ 0x%x]' % disp
while i >= 0:
    for back in range(2, 4):
        j = i - back
        for ins in md.disasm(data[j:j + 16], va0 + j):
            if ins.address in seen: break
            if tag in ins.op_str and ins.address + ins.size > va0 + i + 3:
                dst_mem = ins.op_str.split(',')[0].strip().endswith(tag)
                if not only_write or (dst_mem and ins.mnemonic in ('mov', 'or', 'and', 'add', 'sub', 'inc', 'dec')):
                    print(hex(ins.address), ins.mnemonic, ins.op_str)
                seen.add(ins.address)
            break
    i = data.find(needle, i + 1)
