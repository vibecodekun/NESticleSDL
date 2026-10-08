"""Run original 6502 diagnostic ROMs through the real mapper/PPU callbacks."""
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile


class Diagnostic:
    origin = 0xc100

    def __init__(self):
        self.code = bytearray([0x78, 0xd8, 0xa2, 0xff, 0x9a])
        self.failures = []
        self.checks = []
        self.store(0x2000, 0)
        self.store(0x2001, 0)
        self.store(0x20, 0)

    def store(self, address, value):
        self.code.extend([0xa9, value, 0x8d, address & 255, address >> 8])

    def serial(self, address, value):
        for bit in range(5):
            self.store(address, (value >> bit) & 1)

    def check(self, address, value, description):
        self.code.extend([0xad, address & 255, address >> 8, 0xc9, value, 0xf0, 3, 0x4c, 0, 0])
        self.failures.append(len(self.code) - 2)
        self.code.extend([0xe6, 0x20])
        self.checks.append(description)

    def ppu_address(self, address):
        self.code.extend([0xad, 2, 0x20])  # Reset the two-write address latch.
        self.store(0x2006, address >> 8)
        self.store(0x2006, address & 255)

    def ppu_write(self, address, value):
        self.ppu_address(address)
        self.store(0x2007, value)

    def ppu_check(self, address, value, description):
        self.ppu_address(address)
        self.code.extend([0xad, 7, 0x20])  # Discard the buffered first read.
        self.check(0x2007, value, description)

    def finish(self, banks, chr_pages, mapper=1):
        assert len(self.checks) < 255
        end = self.origin + len(self.code)
        self.code.extend([0x4c, end & 255, end >> 8])
        fail = self.origin + len(self.code)
        self.code.extend([0x4c, fail & 255, fail >> 8])
        for offset in self.failures:
            self.code[offset:offset + 2] = fail.to_bytes(2, 'little')
        assert len(self.code) < 0x3e00
        prg = bytearray()
        for bank in range(banks):
            page = bytearray([0xea] * 0x4000)
            page[0] = 0xa0 ^ bank
            page[0x100:0x100 + len(self.code)] = self.code
            for offset in (0x3ffa, 0x3ffc, 0x3ffe):
                page[offset:offset + 2] = self.origin.to_bytes(2, 'little')
            prg.extend(page)
        chr_data = b''.join(bytes([0x40 + page]) * 0x1000 for page in range(chr_pages * 2))
        return b'NES\x1a' + bytes([banks, chr_pages, mapper << 4, 0]) + bytes(8) + prg + chr_data


def small_mmc1():
    d = Diagnostic()
    d.check(0x8000, 0xa0, 'initial low PRG')
    d.check(0xc000, 0xa3, 'initial fixed PRG')
    for value in (0, 1, 0, 0):
        d.store(0xe000, value)
    d.check(0x8000, 0xa0, 'four writes do not commit')
    d.store(0xefff, 0)
    d.check(0x8000, 0xa2, 'fifth write commits across address aliases')
    d.check(0xc000, 0xa3, 'PRG switch preserves fixed bank')
    d.store(0xe000, 1)
    d.store(0xe000, 1)
    d.store(0x8000, 0x80)
    d.serial(0xe123, 1)
    d.check(0x8000, 0xa1, 'bit 7 discards partial serial value')
    for value in (1, 1, 0, 0):
        d.store(0x8000, value)
    d.store(0xffff, 0)
    d.check(0x8000, 0xa3, 'fifth address selects destination register')
    d.serial(0xe000, 31)
    d.check(0x8000, 0xa3, 'PRG bank wraps without out-of-bounds access')
    d.serial(0x8000, 3)
    d.serial(0xa000, 2)
    d.ppu_check(0, 0x42, '8K CHR low half')
    d.ppu_check(0x1000, 0x43, '8K CHR high half')
    d.serial(0xa000, 3)
    d.ppu_check(0, 0x43, 'historical unaligned 8K CHR selection')
    d.ppu_check(0x1000, 0x44, 'historical consecutive CHR half')
    d.serial(0x8000, 0x13)
    d.serial(0xa000, 5)
    d.serial(0xc000, 2)
    d.ppu_check(0, 0x45, 'independent low 4K CHR')
    d.ppu_check(0x1000, 0x42, 'independent high 4K CHR')
    d.ppu_write(0, 0x77)
    d.ppu_check(0, 0x45, 'CHR ROM cannot be overwritten by CPU')
    d.serial(0x8000, 3)
    d.serial(0xa000, 31)
    d.ppu_check(0, 0x47, 'CHR low wraps to last 4K page')
    d.ppu_check(0x1000, 0, 'out-of-range consecutive CHR half returns zero')
    # Exercise all four nametable aliases, then change mirroring and verify
    # the two physical pages retained independent data.
    for control, expected in [(2, [0x51, 0x62, 0x51, 0x62]),
                              (3, [0x51, 0x51, 0x62, 0x62])]:
        d.serial(0x8000, control)
        if control == 2:
            d.ppu_write(0x2000, 0x51)
            d.ppu_write(0x2400, 0x62)
        for index, value in enumerate(expected):
            d.ppu_check(0x2000 + index * 0x400, value, f'mirroring control {control}, table {index}')
    for control, value in [(0, 0x62), (1, 0x51)]:
        d.serial(0x8000, control)
        for index in range(4):
            d.ppu_check(0x2000 + index * 0x400, value, f'historical single screen {control}, table {index}')
    d.store(0x6000, 0x79)
    d.serial(0xe000, 2)
    d.check(0x6000, 0x79, 'PRG RAM survives ROM bank changes')
    return d, d.finish(4, 4)


def large_mmc1(banks):
    d = Diagnostic()
    d.check(0x8000, 0xa0, 'initial low bank')
    d.check(0xc000, 0xaf, 'historical initial fixed bank 15')
    d.serial(0xe000, 1)
    d.ppu_write(0, 0x55)
    d.ppu_write(0x1000, 0xaa)
    if banks == 32:
        d.serial(0xa000, 16)
        d.check(0x8000, 0xb1, '512K upper PRG region')
        d.check(0xc000, 0xaf, '512K historical fixed bank remains 15')
        d.serial(0xa000, 0)
        d.check(0x8000, 0xa1, '512K return to lower region')
    else:
        d.serial(0x8000, 0x12)
        d.serial(0xa000, 16)
        d.check(0x8000, 0xb1, '1MB region selected by first CHR register')
        d.check(0xc000, 0xbf, '1MB fixed bank in selected region')
        d.serial(0xc000, 16)
        d.check(0x8000, 0x91, '1MB region selected by both CHR registers')
        d.check(0xc000, 0x9f, '1MB final region fixed bank')
        d.serial(0xa000, 0)
        d.check(0x8000, 0x81, '1MB region selected by second CHR register')
        d.check(0xc000, 0x8f, '1MB second region fixed bank')
        d.serial(0x8000, 2)
        d.check(0x8000, 0xa1, '1MB 8K mode uses first CHR register only')
        d.serial(0xa000, 16)
        d.check(0x8000, 0x91, '1MB 8K mode selects final region')
    d.ppu_check(0, 0x55, 'CHR RAM low survives banking')
    d.ppu_check(0x1000, 0xaa, 'CHR RAM high survives banking')
    return d, d.finish(banks, 0)


def legacy_mapper(mapper):
    d = Diagnostic()
    d.check(0x8000, 0xa0, 'initial PRG')
    d.check(0xc000, 0xa3, 'fixed PRG')
    if mapper == 2:
        d.store(0x9000, 2)
        d.check(0x8000, 0xa2, 'UxROM low bank switch')
        d.check(0xc000, 0xa3, 'UxROM fixed high bank')
    else:
        d.store(0x9000, 2)
        d.ppu_check(0, 0x44, 'CNROM low CHR switch')
        d.ppu_check(0x1000, 0x45, 'CNROM high CHR switch')
    return d, d.finish(4, 4, mapper)


def main():
    executable = Path(sys.argv[1]).resolve()
    cases = [('MMC1 serial, CHR and mirroring', small_mmc1()),
             ('MMC1 512K PRG and CHR RAM', large_mmc1(32)),
             ('MMC1 1MB PRG and CHR RAM', large_mmc1(64)),
             ('UxROM regression', legacy_mapper(2)),
             ('CNROM regression', legacy_mapper(3))]
    results = []
    with tempfile.TemporaryDirectory(prefix='nesticle-mappers-') as temporary:
        for index, (name, (diagnostic, image)) in enumerate(cases):
            path = Path(temporary) / f'mapper{index}.nes'
            path.write_bytes(image)
            process = subprocess.run([str(executable), str(path), '--headless', '--frames', '2'],
                                     capture_output=True, text=True, timeout=20)
            output = process.stdout + process.stderr
            match = re.search(r'demo_nmi_count=(\d+)\b', output)
            count = int(match[1]) if match else -1
            if process.returncode != 0 or count != len(diagnostic.checks):
                failed = diagnostic.checks[count] if 0 <= count < len(diagnostic.checks) else 'startup'
                raise AssertionError(f'{name}: {count}/{len(diagnostic.checks)}; failed {failed}\n{output}')
            print(f'PASS {name}: {count} checks executed by Bloodlust N6502')
            results.append({'name': name, 'checks': diagnostic.checks, 'output': output})
    result_path = executable.with_name('mapper-validation.json')
    result_path.write_text(json.dumps(results, indent=2) + '\n')
    print(f'{sum(len(result["checks"]) for result in results)} mapper checks passed; {result_path}')


if __name__ == '__main__':
    main()
