"""Synthetic diagnostics for Windows 0.42 quirks, native files and new mappers."""
from pathlib import Path
import json
import re
import subprocess
import sys
import tempfile
from mapper_test import Diagnostic


class ReleaseDiagnostic(Diagnostic):
    origin = 0xe100

    def finish(self, mapper=0, banks=4, chr_banks=4, flags=0, trainer=None):
        end = self.origin + len(self.code)
        self.code.extend([0x4c, end & 255, end >> 8])
        fail = self.origin + len(self.code)
        self.code.extend([0x4c, fail & 255, fail >> 8])
        for off in self.failures:
            self.code[off:off+2] = fail.to_bytes(2, 'little')
        assert len(self.code) < 0x1e00
        prg = bytearray()
        for i in range(banks*2):
            page = bytearray([0xea]*0x2000)
            page[0] = 0xa0+i
            page[0x100:0x100+len(self.code)] = self.code
            for off in (0x1ffa, 0x1ffc, 0x1ffe):
                page[off:off+2] = self.origin.to_bytes(2, 'little')
            prg.extend(page)
        chr_data = b''.join(bytes([0x40+i])*1024 for i in range(chr_banks*8))
        return b'NES\x1a'+bytes([banks,chr_banks,mapper<<4|flags,0])+bytes(8)+(trainer or b'')+prg+chr_data


def mapper4():
    d=ReleaseDiagnostic()
    d.check(0x8000,0xa6,'MMC3 reset uses final 16K in low window')
    d.check(0xa000,0xa7,'MMC3 reset low second 8K')
    d.store(0x8000,6);d.store(0x8001,3)
    d.check(0x8000,0xa3,'MMC3 bank 6 switches first window')
    d.store(0x8000,0x46)
    d.check(0x8000,0xa3,'MMC3 mode write alone does not remap')
    d.store(0x8001,2)
    d.check(0xc000,0xa2,'MMC3 inverted mode changes only third window')
    d.check(0x8000,0xa3,'MMC3 preserves existing first window')
    d.store(0x8002,7);d.store(0x8003,4)
    d.check(0xc000,0xa2,'MMC3 ignores hardware register aliases')
    for command,page,slots in [(0,11,[(0,10),(1,11)]),(1,15,[(2,14),(3,15)]),
                               (2,19,[(4,19)]),(3,20,[(5,20)]),(4,21,[(6,21)]),(5,22,[(7,22)]),
                               (0x80,3,[(4,2),(5,3)]),(0x84,7,[(2,7)])]:
        d.store(0x8000,command);d.store(0x8001,page)
        for slot,expected in slots:
            d.ppu_check(slot*1024,0x40+expected,f'MMC3 command {command:02x} CHR slot {slot}')
    d.store(0xa000,0);d.ppu_write(0x2000,0x55);d.ppu_write(0x2400,0xaa)
    d.store(0xa000,0);d.ppu_check(0x2800,0x55,'MMC3 vertical mirroring')
    d.store(0xa000,1);d.ppu_check(0x2800,0xaa,'MMC3 horizontal mirroring')
    return d,d.finish(4)


def partial(mapper):
    d=ReleaseDiagnostic()
    if mapper==5:
        d.store(0x5115,3);d.check(0x8000,0xa3,'MMC5 unaligned 16K PRG low')
        d.check(0xa000,0xa4,'MMC5 consecutive 8K PRG')
        d.store(0x5116,1);d.check(0xc000,0xa6,'MMC5 ignores remaining PRG registers')
        d.store(0x5120,12);d.ppu_check(0,0x4c,'MMC5 first partial CHR group')
        d.store(0x5128,13);d.ppu_check(0x1000,0x4d,'MMC5 second partial CHR group')
        d.store(0x5124,25);d.ppu_check(0x1000,0x4d,'MMC5 ignores hardware CHR register 5124')
    elif mapper==6:
        d.check(0xc000,0xa2,'Mapper 6 keeps release 8K/16K reset mixup')
        d.store(0x8000,5);d.check(0x8000,0xa2,'Mapper 6 PRG low bank')
        d.ppu_check(0,0x40,'Mapper 6 PPUDATA ignores CHR ROM selection')
        d.store(0x8001,12);d.check(0x8000,0xa2,'Mapper 6 exact write addresses')
        d.store(0x42ff,0);d.ppu_write(0x2000,0x55);d.ppu_write(0x2400,0xaa)
        d.store(0x42ff,0x10);d.ppu_check(0x2800,0xaa,'Mapper 6 expansion mirroring write')
    elif mapper==7:
        d.store(0x8000,1);d.check(0x8000,0xa4,'AxROM 32K low window')
        d.check(0xc000,0xa6,'AxROM 32K third window')
        # 0.42 loads AxROM in lower single-screen mode (004118e0); bit 4
        # changes then select the upper or lower screen.
        d.ppu_write(0x2000,0x55)
        d.store(0x8000,0x11);d.ppu_write(0x2400,0xaa);d.ppu_check(0x2800,0xaa,'AxROM upper single screen')
        d.store(0x8000,1);d.ppu_check(0x2c00,0x55,'AxROM lower single screen at load')
    elif mapper==8:
        d.check(0xc000,0xa2,'Mapper 8 fixed second 16K at reset')
        d.store(0x8000,18);d.check(0x8000,0xa4,'Mapper 8 partial PRG selection')
        d.ppu_check(0,0x40,'Mapper 8 PPUDATA ignores displayed CHR bank')
    elif mapper==9:
        d.check(0xa000,0xa5,'MMC2 fixed final three 8K pages')
        d.store(0xa000,2);d.check(0x8000,0xa0,'MMC2 ignores hardware PRG address')
        d.store(0xafff,2);d.check(0x8000,0xa2,'MMC2 uses exact AFFF PRG address')
        d.store(0xb000,2);d.ppu_check(0,0x48,'MMC2 B000 changes only first CHR slot')
        d.ppu_check(1024,0x41,'MMC2 B000 leaves second CHR slot')
        d.store(0xc000,3);d.ppu_check(3*1024,0x4f,'MMC2 C000 maps four CHR slots')
    elif mapper==11:
        d.check(0xc000,0xa2,'Mapper 11 fixed second 16K at reset')
        d.store(0xc000,2);d.check(0x8000,0xa4,'Mapper 11 PRG register at C000')
        d.store(0x8000,0x20);d.ppu_check(0,0x50,'Mapper 11 address dependent CHR selection')
    elif mapper==15:
        d.check(0xc000,0xa2,'Mapper 15 reset maps first 32K')
        d.store(0x8000,1);d.check(0x8000,0xa2,'Mapper 15 low paired bank')
        d.check(0xc000,0xa4,'Mapper 15 high paired bank')
        d.store(0x8002,0x81)
        for address in (0x8000,0xa000,0xc000,0xe000):
            d.check(address,0xa3,'Mapper 15 repeats selected odd 8K page')
        d.store(0x8003,2);d.check(0xe000,0xa5,'Mapper 15 updates upper pair')
    return d,d.finish(mapper)


def ppu_quirks():
    d=ReleaseDiagnostic()
    d.check(0x2002,0xc0,'Status reports vblank and hit during blank')
    d.check(0x2002,0xc0,'Status read does not clear vblank')
    d.check(0x2000,0,'Write-only PPU control reads zero')
    d.check(0x2006,0x20,'Unused PPU port returns address high byte')
    d.ppu_write(0,0x55);d.ppu_write(1,0xaa);d.ppu_address(0)
    d.check(0x2007,0x55,'First PPUDATA read returns actual byte')
    d.check(0x2007,0x55,'First PPUDATA read does not advance')
    d.check(0x2007,0xaa,'Subsequent PPUDATA read advances')
    d.ppu_write(0x2000,0x66);d.ppu_check(0x3000,0,'No 3000 nametable mirror')
    d.ppu_write(0x3f00,0x88)
    for a in (0x3f00,0x3f04,0x3f10,0x3f1c):
        d.ppu_check(a,0x88,'Palette zero broadcast retains unmasked value')
    d.ppu_check(0x3f20,0,'No palette address aliases above 3F1F')
    d.store(0x10,0x33);d.store(0x2003,8);d.store(0x4014,8)
    d.store(0x2003,16);d.check(0x2004,0x33,'DMA mirrors low 2K and ignores OAM address')
    return d,d.finish(0,chr_banks=0)


def main():
    exe=Path(sys.argv[1]).resolve();results=[]
    def run(name,args,expected=0):
        p=subprocess.run([str(exe),*map(str,args)],capture_output=True,text=True,timeout=20)
        output=p.stdout+p.stderr
        assert p.returncode==expected,(name,output)
        results.append({'name':name,'output':output});return output
    with tempfile.TemporaryDirectory(prefix='nesticle-042-') as t:
        root=Path(t)
        cases=[('MMC3 quirks',mapper4()),('PPU quirks',ppu_quirks())]
        cases += [(f'Mapper {m} release behavior',partial(m)) for m in (5,6,7,8,9,11,15)]
        d=ReleaseDiagnostic();d.store(0x9000,7);d.check(0x8000,0xa6,'UxROM bank wraps modulo PRG count')
        cases.append(('UxROM release bank wrapping',(d,d.finish(2))))
        d=ReleaseDiagnostic();d.store(0x9000,7);d.ppu_check(0,0x78,'CNROM retains bank bits above bit 1')
        d.store(0x9000,15);d.ppu_check(0,0x78,'CNROM bank wraps modulo CHR count')
        cases.append(('CNROM release bank wrapping',(d,d.finish(3,chr_banks=8))))
        total=0
        for index,(name,(d,image)) in enumerate(cases):
            rom=root/f'diagnostic{index}.nes';rom.write_bytes(image)
            out=run(name,[rom,'--headless','--frames',2])
            count=int(re.search(r'demo_nmi_count=(\d+)',out)[1])
            assert count==len(d.checks),(name,count,d.checks[count] if count<len(d.checks) else out,out)
            total+=count;print(f'PASS {name}: {count} assertions')
        demo=root/'demo.nes'
        subprocess.run([sys.executable,str(Path(__file__).with_name('make_demo.py')),str(demo)],check=True,capture_output=True)
        movie=root/'demo.nsm';state=root/'demo.sta';split=root/'split.sta';resumed=root/'resumed.sta';replayed=root/'replayed.sta'
        run('Movie recording',[demo,'--headless','--frames',120,'--test-input','--record-movie',movie,'--save-state',state])
        out=run('Movie playback',[demo,'--headless','--frames',120,'--play-movie',movie,'--save-state',replayed])
        assert state.read_bytes()==replayed.read_bytes(), 'movie replay diverged'
        assert len(movie.read_bytes())==0x59d0+24+240
        run('State checkpoint',[demo,'--headless','--frames',50,'--save-state',split])
        out=run('State resume',[demo,'--headless','--frames',70,'--load-state',split,'--save-state',resumed])
        assert 'demo_nmi_count=119' in out and len(resumed.read_bytes())==0x59d0
        # 0.42 reads a state into a zeroed buffer without a length check.
        bad=root/'short.sta';bad.write_bytes(state.read_bytes()[:0x2807])
        out=run('Truncated state restores zeros',[demo,'--headless','--frames',1,'--load-state',bad])
        assert 'State restored from short.sta' in out,out
        # Its movie player does require the six timing words after the state.
        bad.write_bytes(state.read_bytes()+bytes(12))
        out=run('Truncated movie rejected',[demo,'--headless','--frames',1,'--play-movie',bad],1)
        assert 'Bad nsm header' in out,out
        print('PASS native states and movie replay')
        # Battery RAM increments once each ROM start, including a soft reset.
        d=ReleaseDiagnostic();d.code.extend([0xee,0,0x60]);rom=root/'battery.nes';rom.write_bytes(d.finish(flags=2))
        saves=root/'saves'
        run('Battery write',[rom,'--headless','--frames',1,'--save-dir',saves])
        assert (saves/'battery.sav').read_bytes()[0]==1
        run('Battery restore and soft reset',[rom,'--headless','--frames',2,'--reset-frame',1,'--save-dir',saves])
        assert (saves/'battery.sav').read_bytes()[0]==3
        assert len((saves/'battery.sav').read_bytes())==8192
        d=ReleaseDiagnostic();d.check(0x7000,0x91,'Trainer copied to 7000')
        rom=root/'trainer.nes';rom.write_bytes(d.finish(flags=4,trainer=bytes([0x91])*512))
        assert 'demo_nmi_count=1' in run('Trainer load',[rom,'--headless','--frames',1])
        print('PASS battery persistence, soft reset and trainer')
        d=ReleaseDiagnostic();d.ppu_write(0,0x55);d.store(0x8000,1);d.ppu_write(0,0xaa)
        d.store(0x8000,0);d.ppu_check(0,0x55,'Mapper 6 CHR RAM bank zero survives')
        d.store(0x8000,1);d.ppu_check(0,0xaa,'Mapper 6 second CHR RAM bank survives')
        rom=root/'chr-ram.nes';rom.write_bytes(d.finish(6,chr_banks=0));state=root/'chr-ram.sta'
        out=run('Mapper 6 banked CHR RAM',[rom,'--headless','--frames',2,'--save-state',state])
        assert 'demo_nmi_count=2' in out and state.read_bytes()[0x2907]==0x55
        print('PASS mapper 6 CHR RAM and native bank-zero snapshot')
        d=ReleaseDiagnostic();d.store(0x4015,8);d.store(0x400c,15);d.store(0x400e,32);d.store(0x400f,8)
        rom=root/'noise.nes';rom.write_bytes(d.finish());raw=root/'noise.raw'
        run('Recovered noise audio',[rom,'--headless','--frames',2,'--audio-dump',raw])
        assert any(raw.read_bytes()) and len(raw.read_bytes())==367*2*2
        d=ReleaseDiagnostic();d.store(0x4011,0);d.code.extend([0xa2,100,0xca,0xd0,0xfd]);d.store(0x4011,255)
        rom=root/'pcm.nes';rom.write_bytes(d.finish());raw=root/'pcm.raw'
        run('PCM disabled by default',[rom,'--headless','--frames',1,'--audio-dump',raw]);assert not any(raw.read_bytes())
        run('Optional direct PCM',[rom,'--headless','--frames',1,'--pcm','--audio-dump',raw]);assert any(raw.read_bytes())
        print('PASS noise and optional direct-write PCM')
        for label,cli,mask,line,want in [('MMC3 one-shot IRQ',True,8,5,1),
                                          ('MMC3 consumes masked IRQ',False,8,5,0),
                                          ('MMC3 ignores blanked IRQ',True,0,5,0),
                                          ('MMC3 extends frame to IRQ line 240',True,8,240,1)]:
            d=ReleaseDiagnostic()
            for address,value in [(0x0400,0xe6),(0x0401,0x21),(0x0402,0x40),(0x21,0),
                                  (0x2001,mask),(0xc000,line),(0xe001,1)]:
                d.store(address,value)
            if cli:d.code.append(0x58)
            image=bytearray(d.finish(4))
            for page in range(8):image[16+page*8192+0x1ffe:16+page*8192+0x2000]=bytes([0,4])
            rom=root/'irq.nes';rom.write_bytes(image);state=root/'irq.sta'
            run(label,[rom,'--headless','--frames',2,'--save-state',state])
            assert state.read_bytes()[0x2021]==want,label
        print('PASS release IRQ scheduling')
    path=exe.with_name('release-validation.json');path.write_text(json.dumps({'mapper_ppu_assertions':total,'scenarios':results},indent=2))
    print(f'{total} mapper/PPU assertions and {len(results)} scenarios passed; {path}')


if __name__=='__main__':
    main()
