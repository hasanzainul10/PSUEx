import struct, sys, pefile
pe = pefile.PE(sys.argv[1], fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
for val in [int(v, 16) for v in sys.argv[2:]]:
    pat = struct.pack('<I', val)
    for s in pe.sections:
        data = s.get_data()
        i = data.find(pat)
        while i >= 0:
            print(hex(val), '<-', hex(base + s.VirtualAddress + i), s.Name.rstrip(b'\0').decode())
            i = data.find(pat, i + 1)
