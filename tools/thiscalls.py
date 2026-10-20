# Find "lea ecx, [reg+DISP]" followed (within a few instructions) by a direct call; print call targets.
import sys, collections, pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32
pe = pefile.PE(sys.argv[1], fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
disp = int(sys.argv[2], 16)
text = next(s for s in pe.sections if s.VirtualAddress <= 0x127B60 < s.VirtualAddress + s.Misc_VirtualSize)
data = text.get_data()
va0 = base + text.VirtualAddress
needle = disp.to_bytes(4, 'little')
md = Cs(CS_ARCH_X86, CS_MODE_32)
targets = collections.defaultdict(list)
i = data.find(needle)
while i >= 0:
    for back in (2, 3):
        j = i - back
        if j < 0: continue
        insns = list(md.disasm(data[j:j + 40], va0 + j))
        if not insns or insns[0].mnemonic != 'lea' or not insns[0].op_str.startswith('ecx,') or insns[0].size != back + 4:
            continue
        for ins in insns[1:5]:
            if ins.mnemonic == 'call' and ins.op_str.startswith('0x'):
                targets[int(ins.op_str, 16)].append(insns[0].address)
                break
    i = data.find(needle, i + 1)
for t, sites in sorted(targets.items(), key=lambda kv: -len(kv[1])):
    print(hex(t), len(sites), ' '.join(hex(s) for s in sites[:6]))
