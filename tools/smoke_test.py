"""Exercise CPU/audio, rendering, serialized controls, and ROM validation."""
from pathlib import Path
import json
import re
import subprocess
import sys
import tempfile

executable=Path(sys.argv[1]).resolve()
generator=Path(__file__).with_name('make_demo.py')
results=[]

def run(name, arguments, expected=0, required=()):
    process=subprocess.run([str(executable), *map(str,arguments)],capture_output=True,text=True,timeout=20)
    output=process.stdout+process.stderr
    if process.returncode != expected or any(text not in output for text in required):
        raise AssertionError(f'{name}: exit {process.returncode}\n{output}')
    results.append({'name':name,'exit_code':process.returncode,'output':output})
    print(f'PASS {name}')
    return output

with tempfile.TemporaryDirectory(prefix='nesticle-test-') as directory:
    fixture=Path(directory)
    demo=fixture/'demo with spaces.nes'
    subprocess.run([sys.executable,str(generator),str(demo)],check=True,capture_output=True)
    run('CPU and pulse audio',['--self-test'],required=['Bloodlust N6502: PASS','Historical pulse mixer: PASS'])
    gui=fixture/'gui.bmp'
    run('classic GUI and mouse',['--headless','--frames','3','--test-gui','--screenshot',gui],required=['GUI mouse close: PASS'])
    assert gui.exists() and gui.stat().st_size>320*240*3  # 0.42's default 320x240
    image=fixture/'demo.bmp'
    output=run('NROM and keyboard serialization',[demo,'--headless','--frames','120','--test-input','--screenshot',image],
               required=['demo_nmi_count=119','sprite_y=38','Keyboard A/B/Select/Start: PASS'])
    assert image.exists() and image.stat().st_size>256*224*3
    assert re.search(r'cpu_pc=8091\b',output),output
    truncated=fixture/'truncated.nes'; truncated.write_bytes(demo.read_bytes()[:32])
    run('truncated ROM rejected',[truncated,'--headless','--frames','1'],1,['error reading rom'])
    bad_header=fixture/'bad-header.nes'; bad_header.write_bytes(b'not a ROM'+bytes(32))
    run('invalid header rejected',[bad_header,'--headless','--frames','1'],1,['Bad ROM header'])
    # 0.42 reports mappers 10, 13 and 14 and runs them on its base mapper.
    unsupported=bytearray(demo.read_bytes()); unsupported[6]=0xa0
    unsupported_path=fixture/'mapper10.nes'; unsupported_path.write_bytes(unsupported)
    run('unsupported mapper on base mapper',[unsupported_path,'--headless','--frames','120'],
        required=['#10: Unknown Mapper not supported','demo_nmi_count=119'])
    nes2=bytearray(demo.read_bytes()); nes2[7]=8
    nes2_path=fixture/'nes2.nes'; nes2_path.write_bytes(nes2)
    run('NES 2.0 automatically translated',[nes2_path,'--headless','--frames','120'],required=['NES 2.0 header translated to iNES in memory','demo_nmi_count=119'])
    assert nes2_path.read_bytes()==nes2, 'NES 2.0 loading changed the ROM file'
    # With 32 or more banks 0.42's MMC1 powers on with bank 15, not the last
    # bank, at $C000. Place the demo's halves in banks 0 and 15.
    data=demo.read_bytes(); prg=data[16:16+0x8000]
    for name, banks, chr_banks in [('large MMC1 with CHR ROM',32,1),('MMC1 above 1MB',255,0)]:
        header=bytearray(data[:16]); header[4]=banks; header[5]=chr_banks; header[6]=0x10
        rom=bytearray(banks*0x4000); rom[:0x4000]=prg[:0x4000]; rom[15*0x4000:16*0x4000]=prg[0x4000:]
        path=fixture/(name+'.nes'); path.write_bytes(header+rom+data[16+0x8000:16+0x8000+chr_banks*0x2000])
        run(name,[path,'--headless','--frames','120'],required=['demo_nmi_count=119'])
    empty=bytearray(data[:16]); empty[4]=0
    empty_path=fixture/'no-prg.nes'; empty_path.write_bytes(empty+data[16+0x8000:])
    run('zero PRG banks rejected',[empty_path,'--headless','--frames','1'],1,['Invalid ROM bank count'])

    # 0.42's options: the command line, an @file include and NEStcl95.ini, which
    # is written in 0.42's layout and read back on the next start.
    import shutil
    bundle=fixture/'bundle'; bundle.mkdir()
    for name in (executable.name,'gui.vol','NESticle.pal'): shutil.copy2(executable.with_name(name),bundle/name)
    app=bundle/executable.name
    (bundle/'extra.cfg').write_text('-vfps 50 ; comment\n-nostatepopup\n')
    def run_app(arguments,expected=0,required=()):
        global executable
        saved,executable=executable,app
        try: return run(*([f'0.42 options {len(results)}',arguments,expected,required]))
        finally: executable=saved
    run_app(['-res','400','300','-hblank','100','@extra.cfg','-bogus','-setkey','KEY1','1','2','3','4','5','6','7','8','9','10','11','12',
             '--headless','--frames','1','--save-config'],required=['Parsing file: extra.cfg','unrecognized arg: bogus'])
    ini=(bundle/'NEStcl95.ini').read_text().splitlines()
    for line in ('; Automatically generated NESticle config file ','-res 400 300','-nostatepopup','-hblank 100','-vfps   50',
                 '-setkey KEY1 1 2 3 4 5 6 7 8 9 10 11 12','-setinput 1 KEY1','-setinput 2 NONE','@NEStcl95.cmd'):
        assert line in ini, (line,ini)
    (bundle/'NEStcl95.cmd').write_text('-res 0 0\n')
    output=run_app(['--headless','--frames','1','--screenshot',fixture/'options.bmp'],required=['Parsing file: NEStcl95.cmd'])
    assert (fixture/'options.bmp').stat().st_size<400*300*3, 'unusable -res did not fall back to 320x240'

output=executable.with_name('validation.json')
output.write_text(json.dumps(results,indent=2))
print(f'{len(results)} checks passed; results saved to {output}')
