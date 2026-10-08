"""Create an original NROM demo for testing; includes no commercial ROM data."""
from pathlib import Path
import sys

class Assembler:
    def __init__(self):
        self.code = bytearray()
        self.labels = {}
        self.fixups = []
    def emit(self, *values): self.code.extend(values)
    def mark(self, name): self.labels[name] = 0x8000 + len(self.code)
    def absolute(self, opcode, address):
        self.emit(opcode, address & 255, address >> 8)
    def reference(self, opcode, name, relative=False):
        self.emit(opcode)
        self.fixups.append((len(self.code), name, relative))
        self.emit(0) if relative else self.emit(0, 0)
    def finish(self):
        for offset, name, relative in self.fixups:
            value = self.labels[name]
            if relative:
                displacement = value - (0x8000 + offset + 1)
                assert -128 <= displacement <= 127
                self.code[offset] = displacement & 255
            else: self.code[offset:offset+2] = value.to_bytes(2, 'little')
        return self.code

a=Assembler()
a.mark('reset')
a.emit(0x78,0xd8,0xa2,0xff,0x9a,0xe8)
a.absolute(0x8e,0x2000); a.absolute(0x8e,0x2001)
a.mark('vblank'); a.absolute(0x2c,0x2002); a.reference(0x10,'vblank',True)
a.emit(0xa9,0x3f); a.absolute(0x8d,0x2006)
a.emit(0xa9,0x00); a.absolute(0x8d,0x2006); a.emit(0xa2,0)
a.mark('palette_loop'); a.reference(0xbd,'palette'); a.absolute(0x8d,0x2007)
a.emit(0xe8,0xe0,32); a.reference(0xd0,'palette_loop',True)
a.emit(0xa9,0x20); a.absolute(0x8d,0x2006)
a.emit(0xa9,0); a.absolute(0x8d,0x2006); a.emit(0xa2,4,0xa0,0)
a.mark('tiles'); a.emit(0xa9,1); a.absolute(0x8d,0x2007)
a.emit(0xc8); a.reference(0xd0,'tiles',True)
a.emit(0xca); a.reference(0xd0,'tiles',True)
# Hide every sprite, then put a test sprite in slot zero.
a.emit(0xa9,255,0xa2,0)
a.mark('oam_clear'); a.absolute(0x9d,0x0200); a.emit(0xe8); a.reference(0xd0,'oam_clear',True)
for address,value in [(0x0200,88),(0x0201,2),(0x0202,0),(0x0203,48),
                      (0x4014,2),(0x4015,1),(0x4000,0xbf),(0x4001,0),
                      (0x4002,0xfd),(0x4003,8),(0x2005,0),(0x2005,0),
                      (0x2000,0x80),(0x2001,0x1e)]:
    a.emit(0xa9,value); a.absolute(0x8d,address)
a.mark('idle'); a.reference(0x4c,'idle')
a.mark('nmi'); a.emit(0x48,0x8a,0x48,0x98,0x48,0xe6,0x20)
for value in (1,0): a.emit(0xa9,value); a.absolute(0x8d,0x4016)
a.emit(0xa2,0)
a.mark('controller'); a.absolute(0xad,0x4016); a.emit(0x29,1,0x95,0x40,0xe8,0xe0,8)
a.reference(0xd0,'controller',True)
if '--two-players' in sys.argv:
    a.emit(0xa2,0)
    a.mark('controller2'); a.absolute(0xad,0x4017); a.emit(0x29,1,0x95,0x50,0xe8,0xe0,8)
    a.reference(0xd0,'controller2',True)
    a.emit(0xa5,0x50); a.reference(0xf0,'no_a2',True); a.emit(0xe6,0x30)
    a.mark('no_a2')
a.absolute(0xee,0x0203)
# Holding A changes the sprite's vertical position, visibly testing input.
a.emit(0xa5,0x40); a.reference(0xf0,'no_a',True); a.absolute(0xce,0x0200)
a.mark('no_a'); a.emit(0xa9,2); a.absolute(0x8d,0x4014)
a.emit(0xa9,8); a.absolute(0x8d,0x4003)
a.emit(0x68,0xa8,0x68,0xaa,0x68,0x40)
a.mark('palette')
a.emit(*([0x0f,0x21,0x11,0x30]*4 + [0x0f,0x16,0x27,0x30]*4))
prg=bytearray([0xea]*32768)
code=a.finish(); prg[:len(code)]=code
for offset,label in [(0x7ffa,'nmi'),(0x7ffc,'reset'),(0x7ffe,'idle')]:
    prg[offset:offset+2]=a.labels[label].to_bytes(2,'little')
chr_data=bytearray(8192)
chr_data[16:32]=bytes([0xaa,0x55]*4 + [0,0,0,0,0,0,0,0])
chr_data[32:48]=bytes([0x3c,0x7e,0xff,0xdb,0xff,0x66,0x3c,0x18]+[0]*8)
header=b'NES\x1a'+bytes([2,1,0,0])+bytes(8)
output=Path(sys.argv[1] if len(sys.argv)>1 else 'demo.nes')
output.parent.mkdir(parents=True, exist_ok=True)
output.write_bytes(header+prg+chr_data)
print(f'Created {output} ({len(code)} bytes of original 6502 code)')
