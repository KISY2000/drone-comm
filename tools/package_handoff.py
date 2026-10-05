"""Create a fresh review ZIP containing sources, evidence and build dependencies.

Exclude installers, caches, native build outputs, and unused CMSIS DSP/examples.
The original project and all dependency download archives remain untouched.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]

def selected(path):
    rel = path.relative_to(ROOT)
    parts = rel.parts
    if any(p in ('__pycache__', '.Xil', '.git') for p in parts) or '.uvguix.' in path.name.lower() or path.suffix.lower() in ('.pyc', '.o', '.exe', '.dll', '.axf', '.elf', '.hex', '.bin', '.bit', '.uvguix', '.uvoptx'):
        return False
    if parts[0] == 'build':
        return False
    if parts[0] != 'deps':
        return True
    if len(parts) == 2:
        return path.suffix == '.json'
    if parts[1] == 'packs':
        return path.name == 'manifest.json'
    if parts[1] == 'archives':
        return False
    if parts[1] == 'CMSIS_5-5.9.0':
        return (len(parts) >= 5 and parts[2:5] == ('CMSIS', 'Core', 'Include')) or path.name in ('LICENSE.txt', 'LICENSE', 'NOTICE', 'README.md') and len(parts) == 3
    return True

def source_files():
    # Do not descend into generated SDK/Vivado/CubeMX workspaces or Git caches.
    for directory, folders, filenames in os.walk(ROOT):
        current = Path(directory)
        folders[:] = [name for name in folders
                      if name not in ('__pycache__', '.Xil', '.git')
                      and not (current == ROOT and name == 'build')
                      and not (current == ROOT/'deps' and name == 'archives')]
        for name in filenames:
            path = current/name
            if selected(path) and name != 'package_manifest.json':
                yield path

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, default=ROOT.parent/'drone_comm_review_20261005_mio_uart.zip')
    args = parser.parse_args()
    target = args.output.resolve()
    if target.exists():
        raise SystemExit(f'Refusing to overwrite: {target}')
    verification = json.loads((ROOT/'reports/verification.json').read_text(encoding='utf-8'))
    platforms = json.loads((ROOT/'reports/platform_compilation.json').read_text(encoding='utf-8'))
    keil = json.loads((ROOT/'reports/keil_project_build/results.json').read_text(encoding='utf-8'))
    zynq = json.loads((ROOT/'reports/zynq_sdk_validation.json').read_text(encoding='utf-8'))
    cubemx = json.loads((ROOT/'platforms/stm32/cubemx/evidence/generated_keil_results.json').read_text(encoding='utf-8'))
    standalone = json.loads((ROOT/'reports/stm32_standalone.json').read_text(encoding='utf-8'))
    if verification.get('status') != 'PASS' or not platforms.get('passed') or not keil.get('passed'):
        raise SystemExit('Final software verification/compile evidence is missing or failed')
    if verification.get('tests', {}).get('platforms', {}).get('status') != 'PASS':
        raise SystemExit('Run tools/run_checks.py --platforms before packaging')
    if verification.get('tests', {}).get('keil_projects', {}).get('status') != 'PASS':
        raise SystemExit('Missing final Keil project verification')
    if not zynq.get('passed'):
        raise SystemExit('Missing real Zynq BSP/ELF validation')
    if not cubemx.get('passed'):
        raise SystemExit('Missing CubeMX base-project generation/link validation')
    if not standalone.get('passed'):
        raise SystemExit('Missing STM32 standalone communication link validation')
    if not standalone.get('baud_460800_checked'):
        raise SystemExit('Missing wired STM32 115200/460800 link validation')
    # The active link is PS UART0 on fixed MIO14/15. Historical UART1 EMIO
    # place/route evidence remains in the archive, but cannot validate this link.
    if (zynq.get('communication_route') != 'uart0_mio14_15'
            or zynq.get('uart_baud_default') != 115200
            or zynq.get('console_uart_output_enabled') is not False
            or sorted(zynq.get('validated_baud_profiles', [])) != [115200, 460800]):
        raise SystemExit('Missing current UART0 MIO route, console isolation or dual-baud BSP/ELF evidence')
    for label, evidence in [('native tests', verification), ('platform compilation', platforms), ('Keil projects', keil), ('Zynq BSP/ELF', zynq), ('CubeMX base projects', cubemx), ('STM32 standalone', standalone)]:
        hashes = evidence.get('source_sha256', {})
        if not hashes:
            raise SystemExit(f'Missing source hashes: {label}')
        for name, expected in hashes.items():
            path = ROOT/name
            if not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
                raise SystemExit(f'Stale {label} evidence: {name}; rerun verification')
    files = sorted(source_files())
    manifest = dict(schema=2, board_tested=False, package_kind='software review and handoff; not flash images',
                    communication_route='uart0_mio14_15', default_uart_baud=115200,
                    historical_evidence='UART1 EMIO pin/place/route reports apply only to the previous wiring',
                    files={p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in files})
    manifest_file = ROOT/'reports/package_manifest.json'
    manifest_file.write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    files.append(manifest_file)
    with zipfile.ZipFile(target, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in files:
            archive.write(path, 'drone_comm/'+path.relative_to(ROOT).as_posix())
    # Verify every archived byte against the selected source; validate CRC too.
    with zipfile.ZipFile(target) as archive:
        if archive.testzip():
            raise SystemExit('Archive CRC failure')
        for path in files:
            name = 'drone_comm/'+path.relative_to(ROOT).as_posix()
            if hashlib.sha256(archive.read(name)).digest() != hashlib.sha256(path.read_bytes()).digest():
                raise SystemExit(f'Archive mismatch: {name}')
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    target.with_suffix('.zip.sha256').write_text(f'{digest}  {target.name}\n', encoding='ascii')
    print(json.dumps(dict(path=str(target), files=len(files), bytes=target.stat().st_size, sha256=digest), ensure_ascii=False, indent=2))

if __name__ == '__main__':
    main()
