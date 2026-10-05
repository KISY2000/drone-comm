"""Build the three communication static libraries with installed Keil uVision.

This verifies real .uvprojx projects and shared core compilation, not a flash image.
Windows batch builds start hidden. Logs and library/source hashes are retained.
"""
import argparse
from datetime import datetime, timezone, timedelta
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
TARGETS = ("ground_f103", "ground_f407", "air_f407")


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--uv4", type=Path, default=Path("C:/Users/Li/AppData/Local/Keil_v5/UV4/UV4.exe"))
    parser.add_argument("--timeout", type=int, default=60)
    args = parser.parse_args()
    if not args.uv4.is_file():
        print("Keil uVision missing:", args.uv4)
        return 2
    if not 1 <= args.timeout <= 60:
        parser.error("--timeout must be between 1 and 60 seconds per target")
    logs = ROOT / "build/keil_project_logs"
    logs.mkdir(parents=True, exist_ok=True)
    results = {"validation": "real UV4 static-library builds; no startup/link/flash or board acceptance",
               "tested_at": datetime.now(timezone(timedelta(hours=8))).isoformat(), "projects": []}
    startup = None
    if sys.platform == "win32":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    for role in TARGETS:
        project = ROOT / "platforms/stm32/templates" / (role + ".uvprojx")
        log = logs / (role + ".log")
        library = ROOT / "build/keil" / role / (role + "_comm.lib")
        # Remove only these known build outputs so old logs/libraries cannot
        # masquerade as a successful new run. Never remove source or directories.
        log.unlink(missing_ok=True)
        library.unlink(missing_ok=True)
        command = [str(args.uv4), "-b", str(project), "-o", str(log), "-j0"]
        try:
            proc = subprocess.run(command, cwd=project.parent, startupinfo=startup,
                                  capture_output=True, text=True, timeout=args.timeout)
            returncode = proc.returncode
            output = (proc.stdout + proc.stderr).strip()
        except subprocess.TimeoutExpired:
            returncode = -1
            output = f"UV4 batch build exceeded {args.timeout}s and was terminated"
        text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
        summary = next((line.strip() for line in text.splitlines()
                        if ' Error(s), ' in line and ' Warning(s).' in line), "")
        passed = returncode == 0 and library.is_file() and "0 Error(s), 0 Warning(s)." in summary
        entry = {"target": role, "command": command, "returncode": returncode, "passed": passed,
                 "output": output, "project": project.relative_to(ROOT).as_posix(),
                 "project_sha256": sha256(project), "log": log.relative_to(ROOT).as_posix(),
                 "library": library.relative_to(ROOT).as_posix(), "summary": summary}
        if library.exists():
            entry["library_sha256"] = sha256(library)
            entry["library_bytes"] = library.stat().st_size
        results["projects"].append(entry)
        print(f"{'PASS' if passed else 'FAIL'} UV4 {role}: {summary or output or 'missing log/library'}", flush=True)
    source_files = list((ROOT / "src").glob("*.c")) + list((ROOT / "include").glob("*.h"))
    source_files += list((ROOT / "platforms").rglob("*.c")) + list((ROOT / "platforms").rglob("*.h"))
    results["source_sha256"] = {p.relative_to(ROOT).as_posix(): sha256(p) for p in source_files}
    results["passed"] = all(p["passed"] for p in results["projects"])
    (logs / "results.json").write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("Report:", logs / "results.json")
    return 0 if results["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
