"""Exercise automatic NES 2.0 loading, loss checks, GUI writes and persistence."""
from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile

from release_test import ReleaseDiagnostic, mapper4, partial


def nes2(image, **fields):
    data = bytearray(image)
    data[7] = data[7] & 0xf0 | 8
    if not data[5]:
        data[11] = 7  # Explicit 8K CHR RAM, rather than iNES's implicit RAM.
    if data[6] & 2:
        data[10] = 0x70  # Explicit 8K battery-backed PRG RAM.
    for index, value in fields.items():
        data[int(index.removeprefix('b'))] = value
    return data


def main():
    output_exe = Path(sys.argv[1]).resolve()
    results = []
    with tempfile.TemporaryDirectory(prefix='nesticle-nes2-') as directory:
        root = Path(directory)
        bundle = root / 'app'
        bundle.mkdir()
        for name in (output_exe.name, 'gui.vol', 'NESticle.pal'):
            shutil.copy2(output_exe.parent / name, bundle / name)
        exe = bundle / output_exe.name

        def run(name, image, expected=0, required=(), extra=(), filename='image.nes'):
            rom = root / filename
            rom.write_bytes(image)
            process = subprocess.run([str(exe), str(rom), '--headless', '--frames', '2',
                                      '--save-dir', str(root / 'saves'), *map(str, extra)],
                                     capture_output=True, text=True, timeout=20)
            output = process.stdout + process.stderr
            assert process.returncode == expected, (name, process.returncode, output)
            assert all(text in output for text in required), (name, required, output)
            assert rom.read_bytes() == image, f'{name}: loader changed ROM file'
            results.append({'name': name, 'output': output})
            print(f'PASS {name}')
            return output

        # Check the actual CPU/PPU state, not just whether the header was accepted.
        for mapper in (0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 15):
            if mapper == 4:
                _, image = mapper4()
            elif mapper in (5, 6, 7, 8, 9, 11, 15):
                _, image = partial(mapper)
            else:
                d = ReleaseDiagnostic()
                d.check(0xc000, 0xa6, 'Fixed final bank')
                image = d.finish(mapper)
            run(f'iNES mapper {mapper} baseline', image, extra=['--save-state', root / 'legacy.sta'])
            run(f'NES 2.0 mapper {mapper} parity', nes2(image),
                required=['NES 2.0 header translated to iNES in memory'],
                extra=['--save-state', root / 'translated.sta'])
            assert (root / 'legacy.sta').read_bytes() == (root / 'translated.sta').read_bytes(), mapper

        image = ReleaseDiagnostic().finish(banks=2, chr_banks=1)
        data = nes2(image)
        run('Exponent PRG and CHR sizes', nes2(image, b4=15 << 2, b5=13 << 2, b9=0xff),
            extra=['--save-state', root / 'exponent.sta'])
        run('Linear PRG and CHR sizes', data, extra=['--save-state', root / 'linear.sta'])
        assert (root / 'exponent.sta').read_bytes() == (root / 'linear.sta').read_bytes()
        # A non-power-of-two multiplier must place CHR after the full PRG payload.
        image3 = ReleaseDiagnostic().finish(banks=3, chr_banks=1)
        run('Exponent size with multiplier three', nes2(image3, b4=(14 << 2) | 1, b9=0x0f),
            extra=['--save-state', root / 'exponent.sta'])
        run('Linear size with three banks', nes2(image3), extra=['--save-state', root / 'linear.sta'])
        assert (root / 'exponent.sta').read_bytes() == (root / 'linear.sta').read_bytes()

        rejected = [
            ('Extended mapper does not alias NROM', {'b8': 1}, 'Mapper 256 is not supported'),
            ('High mapper nibble does not alias NROM', {'b7': 0x18}, 'Mapper 16 is not supported'),
            ('Unsupported old mapper', {'b6': 0xa0}, 'Mapper 10 is not supported'),
            ('Extended PRG size does not truncate', {'b9': 1}, 'limit of 255'),
            ('Extended CHR size does not truncate', {'b9': 0x10}, 'limit of 255'),
            ('Unaligned exponent PRG', {'b4': 13 << 2, 'b9': 15}, 'cannot be represented'),
            ('Unaligned exponent CHR', {'b5': 12 << 2, 'b9': 0xf0}, 'cannot be represented'),
            ('Huge exponent with overflow', {'b4': 255, 'b9': 15}, 'cannot be represented'),
            ('Huge exponent without overflow', {'b4': 252, 'b9': 15}, 'limit of 255'),
            ('No PRG data', {'b4': 0}, 'cannot be represented'),
            ('Unsupported mapper variant', {'b6': 0x40, 'b8': 0x30}, 'mapper 4 submapper 3'),
            ('Vs. console', {'b7': 9}, 'console type 1'),
            ('PlayChoice console', {'b7': 10}, 'console type 2'),
            ('Extended console', {'b7': 11}, 'console type 3'),
            ('Banked PRG RAM', {'b10': 8}, 'PRG RAM layout'),
            ('Banked PRG NVRAM', {'b6': 2, 'b10': 0x80}, 'PRG RAM layout'),
            ('Separate PRG RAM and NVRAM', {'b6': 2, 'b10': 0x77}, 'PRG RAM layout'),
            ('Mixed CHR ROM and RAM', {'b11': 7}, 'CHR RAM layout'),
            ('CHR NVRAM', {'b11': 0x70}, 'CHR RAM layout'),
            ('Banked CHR RAM', {'b5': 0, 'b11': 8}, 'CHR RAM layout'),
            ('Missing CHR memory', {'b5': 0, 'b11': 0}, 'neither CHR ROM nor CHR RAM'),
            ('Unknown persistent storage', {'b6': 2, 'b10': 0}, 'non-volatile storage'),
            ('Miscellaneous ROMs', {'b14': 1}, 'miscellaneous ROM data'),
        ]
        for name, fields, message in rejected:
            run(name, nes2(image, **fields), expected=1, required=[message])
        run('Truncated PRG rejected', data[:32], expected=1, required=['error reading rom'])
        run('Truncated CHR rejected', data[:-1], expected=1, required=['error reading vrom'])
        # 0.42 drops an unreadable trainer and fails on the PRG read that follows.
        run('Truncated trainer rejected', nes2(image, b6=4)[:32], expected=1, required=['error reading rom'])
        for mapper in (2, 3, 7):
            for submapper in (1, 2):
                run(f'Legacy bus conflicts mapper {mapper} variant {submapper}',
                    nes2(ReleaseDiagnostic().finish(mapper), b8=submapper << 4),
                    required=[f'submapper {submapper}: using legacy mapper {mapper}'])
        run('PAL hint uses Timing settings', nes2(image, b12=1), required=['PAL timing hint'])
        run('Dendy hint uses Timing settings', nes2(image, b12=3), required=['Dendy timing hint'])
        run('Controller metadata uses configured devices', nes2(image, b15=7), required=['expansion device 7'])

        d = ReleaseDiagnostic()
        d.check(0x7000, 0x91, 'Trainer payload at correct offset')
        trainer_image = d.finish(flags=4, trainer=bytes([0x91]) * 512)
        run('NES 2.0 trainer load', nes2(trainer_image), required=['demo_nmi_count=1'])
        d = ReleaseDiagnostic()
        d.code.extend([0xee, 0, 0x60])
        battery_image = nes2(d.finish(flags=2))
        run('NES 2.0 battery write', battery_image, filename='battery.nes')
        assert (root / 'saves/battery.sav').read_bytes()[0] == 1
        run('NES 2.0 battery restore', battery_image, filename='battery.nes')
        assert (root / 'saves/battery.sav').read_bytes()[0] == 2
        run('NVRAM enables saves even if battery flag is missing', nes2(image, b10=0x70),
            required=['enabling battery saves'], filename='nvram.nes')
        assert (root / 'saves/nvram.sav').stat().st_size == 8192

        # GUI writes are explicit, and must preserve all untouched NES 2.0 fields.
        for name, header_image in (
            ('linear', nes2(image, b12=2, b15=1)),
            ('exponent', nes2(image, b4=15 << 2, b5=13 << 2, b9=0xff, b12=2, b15=1)),
            ('submapper', nes2(ReleaseDiagnostic().finish(2, chr_banks=0), b8=0x20)),
            ('NVRAM with missing battery flag', nes2(image, b10=0x70)),
        ):
            rom = root / 'edited.nes'
            rom.write_bytes(header_image)
            process = subprocess.run([str(exe), str(rom), '--headless', '--frames', '2',
                                      '--inspect-view', 'header', '--test-dialog-action', 'header-change'],
                                     capture_output=True, text=True, timeout=20)
            output = process.stdout + process.stderr
            assert process.returncode == 0 and 'GUI dialog action header: PASS' in output, output
            expected = bytearray(header_image)
            expected[6] ^= 1
            assert rom.read_bytes() == expected, f'{name}: GUI lost NES 2.0 metadata'
            results.append({'name': f'GUI preserves {name} NES 2.0 header', 'output': output})
            print(f'PASS GUI preserves {name} NES 2.0 header')

        for name, header_image, ram_byte in (
            ('enable', nes2(image, b10=7), 0x70),
            ('disable', nes2(image, b6=2, b10=0x70), 7),
        ):
            rom = root / 'battery-edited.nes'
            rom.write_bytes(header_image)
            process = subprocess.run([str(exe), str(rom), '--headless', '--frames', '2',
                                      '--save-dir', str(root / 'saves'), '--inspect-view', 'header',
                                      '--test-dialog-action', 'header-battery-change'],
                                     capture_output=True, text=True, timeout=20)
            output = process.stdout + process.stderr
            assert process.returncode == 0 and 'GUI dialog action header: PASS' in output, output
            expected = bytearray(header_image)
            expected[6] ^= 2
            expected[10] = ram_byte
            assert rom.read_bytes() == expected, f'{name}: GUI battery field inconsistent'
            assert output.count('NES 2.0 header translated') == 2, output
            results.append({'name': f'GUI {name} NES 2.0 battery', 'output': output})
            print(f'PASS GUI {name} NES 2.0 battery')

        sidecar_rom = root / 'sidecar.nes'
        sidecar_rom.with_suffix('.hdr').write_bytes(data[:16])
        run('NES 2.0 alternate header', image, filename=sidecar_rom.name,
            required=['Alternate header read', 'NES 2.0 header translated'])
        assert sidecar_rom.with_suffix('.hdr').read_bytes() == data[:16]

    report = output_exe.with_name('nes2-validation.json')
    report.write_text(json.dumps(results, indent=2))
    print(f'{len(results)} NES 2.0 checks passed; {report}')


if __name__ == '__main__':
    main()
