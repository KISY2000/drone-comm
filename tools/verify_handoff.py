"""Rebuild a clean review ZIP using installed tools; never overwrite a checkout."""
import argparse
from datetime import datetime, timezone, timedelta
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--destination', type=Path, required=True)
    args = parser.parse_args()
    archive, destination = args.archive.resolve(), args.destination.resolve()
    if destination.exists():
        parser.error('Destination must be new; existing files are never replaced')
    with zipfile.ZipFile(archive) as zipped:
        if zipped.testzip():
            raise RuntimeError('ZIP CRC failure')
        for name in zipped.namelist():
            target = (destination / name).resolve()
            if destination not in target.parents:
                raise RuntimeError('Unsafe archive path: ' + name)
        zipped.extractall(destination)
    extracted = destination / 'drone_comm'
    manifest = json.loads((extracted/'reports/package_manifest.json').read_text(encoding='utf-8'))
    for name, expected in manifest['files'].items():
        if sha(extracted/name) != expected:
            raise RuntimeError('Manifest mismatch: ' + name)
    command = [sys.executable, '-X', 'utf8', 'tools/run_checks.py', '--platforms']
    print('Rebuilding extracted sources:', extracted, flush=True)
    # Child checks have bounded subprocess timeouts; the calling tool yields while
    # this actual compilation continues. The captured log is retained for review.
    result = subprocess.run(command, cwd=extracted, capture_output=True, text=True,
                            encoding='utf-8', errors='replace', timeout=600)
    (destination/'rebuild.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    if result.returncode:
        raise RuntimeError('Extracted build failed; see ' + str(destination/'rebuild.log'))
    report = json.loads((extracted/'reports/verification.json').read_text(encoding='utf-8'))
    assert report['status'] == 'PASS'
    assert all(item['status'] == 'PASS' for item in report['tests'].values())
    for name, expected in report['source_sha256'].items():
        assert sha(ROOT/name) == expected, 'Extracted sources differ: ' + name
    source = extracted/'src/dc_protocol.c'
    original = source.read_bytes()
    try:
        source.write_bytes(original + b'\n/* isolated stale-evidence test */\n')
        probe = subprocess.run([sys.executable, '-X', 'utf8', 'tools/package_handoff.py',
                                '--output', 'build/stale_evidence_probe.zip'], cwd=extracted,
                               capture_output=True, text=True, encoding='utf-8', timeout=30)
        diagnostic = (probe.stdout + probe.stderr).strip()
        assert probe.returncode != 0 and 'Stale native tests evidence: src/dc_protocol.c' in diagnostic
        assert not (extracted/'build/stale_evidence_probe.zip').exists()
    finally:
        source.write_bytes(original)
    platform = json.loads((extracted/'reports/platform_compilation.json').read_text(encoding='utf-8'))
    keil = json.loads((extracted/'reports/keil_project_build/results.json').read_text(encoding='utf-8'))
    evidence = dict(schema=2, tested_at=datetime.now(timezone(timedelta(hours=8))).isoformat(),
        status='PASS', board_tested=False,
        scope='clean ZIP extraction; native tests including bounded RAM logging, real ARM object checks, three UV4 communication libraries, three standalone applications (disabled/enabled-link-only plus wired 460800 profiles) and two CubeMX base-project rebuilds on installed local compilers',
        exclusions='Does not rerun Vivado/XSCT/CubeMX generation; CubeMX generated C projects are rebuilt; saved hardware-generation evidence is checked against the package manifest',
        archive=str(archive), archive_sha256=sha(archive), extracted_root=str(extracted),
        native_tests={k:v['status'] for k,v in report['tests'].items()},
        target_object_checks=len(platform['checks']), target_object_checks_passed=platform['passed'],
        keil_projects={p['target']:p['summary'] for p in keil['projects']},
        source_hashes_match_original=True,
        stale_evidence_guard=dict(status='PASS', rejected_changed_source=True, diagnostic=diagnostic),
        source_sha256=report['source_sha256'])
    (ROOT/'reports/handoff_portability.json').write_text(json.dumps(evidence, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print('PASS: extracted rebuild, source equality and stale-evidence rejection')


if __name__ == '__main__':
    main()
