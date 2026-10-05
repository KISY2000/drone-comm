"""Build and run native tests; record evidence without claiming board execution."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import random
import shutil
import subprocess
import sys
from datetime import datetime, timezone, timedelta

ROOT = Path(__file__).resolve().parents[1]
GCC_DEFAULT = Path(r'D:\Xilinx\Vivado\2017.4\tps\mingw\6.2.0\win64.o\nt\bin\gcc.exe')

def checked(command, **kwargs):
    timeout = kwargs.pop('timeout', 60)
    result = subprocess.run([str(x) for x in command], cwd=ROOT, capture_output=True, text=True, timeout=timeout, **kwargs)
    if result.returncode:
        raise RuntimeError(f'Failed ({result.returncode}): {command}\n{result.stdout}\n{result.stderr}')
    return result.stdout.strip()

def crosscheck(cli):
    spec = importlib.util.spec_from_file_location('reference_codec', ROOT / 'tools/reference_codec.py')
    ref = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ref)
    fixtures = json.loads((ROOT / 'tests/golden_frames.json').read_text(encoding='utf-8'))
    pairs = [(bytes.fromhex(f['raw']), bytes.fromhex(f['uart'])) for f in fixtures]
    for raw, uart in pairs:
        assert ref.uart_frame(raw) == uart and ref.decode_uart(uart) == raw
    rng = random.Random(20261005)
    for size in range(33):
        for _ in range(4):
            payload = bytes(rng.randrange(256) for _ in range(size))
            raw = ref.raw_frame(rng.choice([1, 3, 0x10, 0x11, 0x12, 0x20]), rng.randrange(1, 5), rng.randrange(1, 5), rng.getrandbits(32), rng.getrandbits(16), payload)
            pairs.append((raw, ref.uart_frame(raw)))
    raw_lines = '\n'.join(raw.hex() for raw, _ in pairs) + '\n'
    uart_lines = '\n'.join(uart.hex() for _, uart in pairs) + '\n'
    actual = checked([cli, 'encode'], input=raw_lines).splitlines()
    assert actual == [uart.hex() for _, uart in pairs], 'C UART encoding differs from independent Python'
    actual = checked([cli, 'decode'], input=uart_lines).splitlines()
    assert actual == [raw.hex() for raw, _ in pairs], 'C decode differs from golden/reference'
    bad = []
    for raw, _ in pairs[:20]:
        mutated = bytearray(raw); mutated[-1] ^= 1; bad.append(mutated.hex())
    assert checked([cli, 'encode'], input='\n'.join(bad)+'\n').splitlines() == ['ERROR'] * len(bad)
    return dict(status='PASS', golden_frames=len(fixtures), random_frames=132, crc_corruptions=len(bad), crc_standard='123456789 -> 29b1')

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--gcc', type=Path, default=GCC_DEFAULT)
    parser.add_argument('--platforms', action='store_true', help='Also compile real HAL/SDK adapter dependencies')
    args = parser.parse_args()
    build = ROOT / 'build'; build.mkdir(exist_ok=True)
    (ROOT/'reports').mkdir(exist_ok=True)
    report = dict(schema=1, tested_at=datetime.now(timezone(timedelta(hours=8))).isoformat(), native_compiler=checked([args.gcc, '--version']).splitlines()[0], board_tested=False, tests={})
    tests = {
        'independent_review': ['src/dc_protocol.c', 'src/dc_node.c', 'tests/test_review.c'],
        'radio_mock': ['src/dc_protocol.c', 'src/dc_radio.c', 'tests/test_radio.c'],
        'four_nodes': ['src/dc_protocol.c', 'src/dc_node.c', 'tests/sim_four_nodes.c'],
        'byte_ring': ['platforms/common/dc_byte_ring.c', 'platforms/common/test_byte_ring.c'],
        'codec_cli': ['src/dc_protocol.c', 'tests/codec_cli.c'],
        'app_binding_mock': ['src/dc_protocol.c', 'src/dc_node.c', 'platforms/stm32/common/dc_stm32_app.c', 'platforms/stm32/tests/test_app_binding.c'],
        'spi_poll_fixture': ['platforms/stm32/tests/test_spi_poll.c'],
        'zynq_ram_log': ['platforms/zynq/dc_zynq_log.c', 'tests/test_zynq_log.c'],
    }
    try:
        for name, sources in tests.items():
            exe = build / (name + '.exe')
            extra = ['-DDC_STM32_F1', '-I', ROOT/'platforms/stm32/tests/mock', '-I', ROOT/'platforms/stm32/common'] if name == 'app_binding_mock' else []
            if name == 'spi_poll_fixture':
                extra = ['-I', ROOT/'platforms/stm32/common']
            if name == 'zynq_ram_log':
                extra = ['-I', ROOT/'platforms/zynq']
            checked([args.gcc, '-std=c99', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O2', '-I', ROOT/'include', '-I', ROOT/'platforms/common', *extra, *sources, '-o', exe])
            if name != 'codec_cli':
                report['tests'][name] = dict(status='PASS', output=checked([exe]))
        report['tests']['cross_language'] = crosscheck(build/'codec_cli.exe')
        if args.platforms:
            report['tests']['platforms'] = dict(status='PASS', output=checked([sys.executable, ROOT/'tools/check_platforms.py']))
            shutil.copyfile(ROOT/'build/platform_checks/results.json', ROOT/'reports/platform_compilation.json')
            report['tests']['keil_projects'] = dict(status='PASS', output=checked([sys.executable, ROOT/'tools/check_keil_projects.py'], timeout=190))
            destination = ROOT/'reports/keil_project_build'
            destination.mkdir(exist_ok=True)
            for path in (ROOT/'build/keil_project_logs').glob('*'):
                if path.suffix in ('.log', '.json'):
                    shutil.copyfile(path, destination/path.name)
            report['tests']['stm32_standalone'] = dict(status='PASS', output=checked(
                [sys.executable, ROOT/'tools/check_stm32_standalone.py', '--baud-460800'], timeout=490))
            report['tests']['cubemx_base'] = dict(status='PASS', output=checked(
                [sys.executable, ROOT/'platforms/stm32/cubemx/verify.py'], timeout=130))
        report['status'] = 'PASS'
    except Exception as exc:
        report['status'] = 'FAIL'; report['error'] = str(exc)
    paths = [p for folder in ('src', 'include', 'tests', 'platforms', 'tools')
             for p in (ROOT/folder).rglob('*')
             if p.is_file() and '__pycache__' not in p.parts and 'evidence' not in p.relative_to(ROOT).parts
             and p.suffix.lower() in ('.c', '.h', '.py', '.json', '.uvprojx', '.template',
                                      '.tcl', '.bd', '.ioc', '.s', '.ld', '.sct', '.ps1', '.xdc')]
    report['source_sha256'] = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths if p.is_file()}
    (ROOT/'reports/verification.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if report['status'] == 'PASS' else 1

if __name__ == '__main__':
    raise SystemExit(main())
