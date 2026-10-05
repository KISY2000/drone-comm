"""Rebuild/link disabled and enabled-for-link-only STM32 communication apps.

No download/debug command is used. Enabled projects and executable images stay
under build/, not in the handoff templates. Every project enforces scatter limits.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parent.parent
TEMPLATES = ROOT / "platforms/stm32/standalone"
REPORTS = ROOT / "reports/stm32_standalone"
ROLES = ("ground_f103", "ground_f407", "air_f407")
MODES = ("default_disabled", "enabled_link_only")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(condition, message):
    if not condition:
        raise ValueError(message)


def relative(path, parent):
    return os.path.relpath(path, parent).replace("/", "\\")


def relocated_project(role, mode="enabled_link_only"):
    """Clone only the project: code and the disabled delivery template stay intact."""
    source = TEMPLATES / (role + ".uvprojx")
    destination = ROOT / "build/stm32_standalone" / mode / "projects" / (role + ".uvprojx")
    destination.parent.mkdir(parents=True, exist_ok=True)
    tree = ET.parse(source)
    project = tree.getroot()
    for node in project.findall(".//FilePath") + project.findall(".//ScatterFile"):
        original = (source.parent / node.text.replace("\\", "/")).resolve()
        check(original.is_file(), f"Missing staged project input: {original}")
        node.text = relative(original, destination.parent)
    for node in project.findall(".//IncludePath"):
        if node.text:
            includes = [(source.parent / item.replace("\\", "/")).resolve()
                        for item in node.text.split(";")]
            node.text = ";".join(relative(path, destination.parent) for path in includes)
    project.find(".//TargetName").text = role + "_" + mode + "_DO_NOT_FLASH"
    defines = project.find(".//Cads/VariousControls/Define")
    check("DC_BOARD_READY=0" in defines.text, "Delivery template must remain disabled")
    defines.text = defines.text.replace("DC_BOARD_READY=0", "DC_BOARD_READY=1")
    if mode == "enabled_link_only_460800":
        defines.text += " DC_UART_BAUD=460800u"
    project.find(".//OutputName").text = role + "_LINK_ONLY_DO_NOT_FLASH"
    output = ROOT / "build/stm32_standalone" / mode / role
    for tag in ("OutputDirectory", "ListingPath"):
        project.find(".//" + tag).text = relative(output, destination.parent) + "\\"
    check(project.find(".//CreateHexFile").text == "0", "HEX must stay disabled")
    ET.indent(tree)
    tree.write(destination, encoding="utf-8", xml_declaration=True)
    return destination


def memory_report(text, role, enabled):
    expected_flash = 0x10000 if role == "ground_f103" else 0x100000
    expected_ram = 0x5000 if role == "ground_f103" else 0x20000
    load_match = re.search(r"Load Region LR_IROM1 \(Base: 0x([\da-fA-F]+), Size: 0x([\da-fA-F]+), Max: 0x([\da-fA-F]+)", text)
    ram_match = re.search(r"Execution Region RW_IRAM1 \(Exec base: 0x([\da-fA-F]+), Load base: 0x[\da-fA-F]+, Size: 0x([\da-fA-F]+), Max: 0x([\da-fA-F]+)", text)
    check(load_match and ram_match, "Missing actual link memory regions in map")
    flash_base, flash_used, flash_limit = (int(value, 16) for value in load_match.groups())
    ram_base, ram_used, ram_limit = (int(value, 16) for value in ram_match.groups())
    check((flash_base, flash_limit) == (0x08000000, expected_flash), "Actual Flash bounds differ from board limits")
    check((ram_base, ram_limit) == (0x20000000, expected_ram), "Actual SRAM bounds differ from board limits")
    check(flash_used <= flash_limit and ram_used <= ram_limit, "Actual link memory limit exceeded")
    stack = re.search(r"^\s*(?:Stack_Mem|STACK)\s+0x([\da-fA-F]+)\s+(?:Data|Section)\s+(\d+)\s+", text, re.M)
    check(stack and int(stack.group(2)) == 4096, "Map must retain the 4096-byte startup stack")
    symbols = ["Reset_Handler", "SystemInit", "SysTick_Handler", "HAL_IncTick", "main"]
    if enabled:
        symbols += ["dc_board_peripherals_init", "dc_firmware_start", "dc_firmware_poll",
                    "dc_stm32_app_init", "dc_stm32_app_poll", "dc_node_receive"]
        if role != "ground_f407": symbols += ["dc_radio_init", "dc_stm32_radio_port"]
        else:
            symbols += ["huart2", "huart3", "hdma_usart2_rx", "hdma_usart3_rx"]
            for irq in ["USART2_IRQHandler", "USART3_IRQHandler", "DMA1_Stream1_IRQHandler", "DMA1_Stream5_IRQHandler"]:
                check(re.search(r"^\s*" + irq + r"\s+0x[\da-fA-F]+\s+Thumb Code\s+\d+\s+board_peripherals\.o", text, re.M),
                      f"New route IRQ must resolve to board implementation: {irq}")
            check(not re.search(r"^\s*huart6\s+0x", text, re.M), "Obsolete USART6 handle remains linked")
    for symbol in symbols:
        check(re.search(r"^\s*" + re.escape(symbol) + r"\s+0x[\da-fA-F]+\s+", text, re.M),
              f"Required live link symbol missing: {symbol}")
    return {"flash_base": f"0x{flash_base:08X}", "flash_bytes_used": flash_used,
            "flash_bytes_limit": flash_limit, "flash_bytes_remaining": flash_limit - flash_used,
            "ram_base": f"0x{ram_base:08X}", "ram_bytes_used_including_stack": ram_used,
            "ram_bytes_limit": ram_limit, "ram_bytes_remaining": ram_limit - ram_used,
            "stack_bytes": 4096, "heap_bytes": 0, "live_symbols_checked": symbols,
            "ccm_or_external_memory_used": False,
            "runtime_stack_high_water_tested": False}


def build_one(args, role, mode):
    project = (TEMPLATES / (role + ".uvprojx") if mode == "default_disabled"
               else relocated_project(role, mode))
    xml = ET.parse(project).getroot()
    check(xml.find(".//CreateHexFile").text == "0", "No HEX generation is allowed")
    check(xml.find(".//CreateExecutable").text == "1" and xml.find(".//CreateLib").text == "0",
          "Standalone project must produce a linked executable")
    flags = xml.find(".//Cads/VariousControls/Define").text
    check("DC_BOARD_READY=" + ("0" if mode == "default_disabled" else "1") in flags,
          "Unexpected board-ready mode")
    check("HSE_VALUE=8000000U" in flags, "SystemInit must compile for 8 MHz HSE")
    output = (project.parent / xml.find(".//OutputDirectory").text.replace("\\", "/")).resolve()
    check(output.is_relative_to(ROOT / "build/stm32_standalone"), "Unsafe standalone output location")
    output.mkdir(parents=True, exist_ok=True)
    output_name = xml.find(".//OutputName").text
    image = output / (output_name + ".axf")
    link_map = output / (output_name + ".map")
    log = output / "build.log"
    # These are known single-file build products. No recursive removal is used.
    for path in (image, link_map, log): path.unlink(missing_ok=True)
    startup = None
    if sys.platform == "win32":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = subprocess.SW_HIDE
    command = [str(args.uv4), "-r", str(project), "-o", str(log), "-j0"]
    entry = {"role": role, "mode": mode, "board_ready": mode != "default_disabled",
             "uart_baud": 460800 if mode == "enabled_link_only_460800" else 115200,
             "board_tested": False, "passed": False, "hex_generated": False,
             "project": project.relative_to(ROOT).as_posix(), "project_sha256": sha(project),
             "compiler_defines": flags, "command": command}
    try:
        completed = subprocess.run(command, cwd=project.parent, capture_output=True,
                                   text=True, errors="replace", startupinfo=startup,
                                   timeout=args.timeout)
        entry["returncode"] = completed.returncode
        text = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
        summaries = re.findall(r"(\d+) Error\(s\), (\d+) Warning\(s\)\.", text)
        check(summaries, "Missing fresh Keil build summary")
        errors, warnings = map(int, summaries[-1])
        entry["summary"] = f"{errors} Error(s), {warnings} Warning(s)."
        entry["warnings"] = warnings
        entry["warning_lines"] = [line for line in text.splitlines() if "warning:" in line.lower()]
        entry["warning_note"] = (
            "Warnings are retained verbatim. F4 currently has three official HAL flash_ex.c unused-parameter warnings and one MDK assembly-preprocessor target-option warning. No diagnostic is suppressed."
            if warnings else "No warnings in the actual build log.")
        check(completed.returncode in (0, 1) and errors == 0, f"Keil build failed: {entry['summary']}")
        check(image.is_file() and link_map.is_file(), "Fresh AXF/map missing after build")
        check(not list(output.glob("*.hex")), "Unexpected standalone HEX output")
        entry["image"] = {"path": image.relative_to(ROOT).as_posix(), "sha256": sha(image),
                           "bytes": image.stat().st_size, "flashable_delivery": False}
        map_text = link_map.read_text(encoding="utf-8", errors="replace")
        entry["memory"] = memory_report(map_text, role, mode != "default_disabled")
        entry["passed"] = True
    except (ValueError, OSError, subprocess.SubprocessError) as exc:
        entry["error"] = str(exc)
    evidence_dir = REPORTS / mode
    evidence_dir.mkdir(parents=True, exist_ok=True)
    evidence = {}
    inputs = [(log, role + ".log"), (link_map, role + ".map"), (project, role + ".uvprojx"),
              (output / (output_name + ".lnp"), role + ".lnp")]
    inputs += [(path, role + "_dependencies/" + path.name) for path in sorted(output.glob("*.d"))]
    for source, name in inputs:
        if source.is_file():
            target = evidence_dir / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(source.read_bytes())
            evidence[target.relative_to(ROOT).as_posix()] = sha(target)
    entry["evidence_sha256"] = evidence
    print(f"{'PASS' if entry['passed'] else 'FAIL'} standalone {role} {mode}: "
          f"{entry.get('summary', '')} {entry.get('error', '')}".strip(), flush=True)
    return entry


def source_hashes():
    files = [Path(__file__), TEMPLATES / "create_projects.py"]
    for directory, suffixes in (
        (ROOT / "src", (".c",)), (ROOT / "include", (".h",)),
        (ROOT / "platforms/common", (".c", ".h")),
        (ROOT / "platforms/stm32/common", (".c", ".h")),
        (ROOT / "platforms/stm32/f1", (".h",)), (ROOT / "platforms/stm32/f4", (".h",)),
        (TEMPLATES, (".c", ".s", ".sct", ".uvprojx", ".json")),
        (ROOT / "deps/stm32f1xx-hal-driver-1.1.10", (".c", ".h")),
        (ROOT / "deps/stm32f4xx-hal-driver-1.8.5", (".c", ".h")),
        (ROOT / "deps/cmsis-device-f1-4.3.5/Include", (".h",)),
        (ROOT / "deps/cmsis-device-f4-2.6.11/Include", (".h",)),
        (ROOT / "deps/CMSIS_5-5.9.0/CMSIS/Core/Include", (".h",)),
    ):
        files += [path for path in directory.rglob("*") if path.is_file() and path.suffix in suffixes]
    for family, version, device in (("f1", "4.3.5", "stm32f103xb"), ("f4", "2.6.11", "stm32f407xx")):
        prefix = ROOT / f"deps/cmsis-device-{family}-{version}/Source/Templates"
        files += [prefix / f"system_stm32{family}xx.c", prefix / f"arm/startup_{device}.s"]
    return {path.relative_to(ROOT).as_posix(): sha(path) for path in sorted(set(files))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--uv4", type=Path, default=Path(r"C:\Users\Li\AppData\Local\Keil_v5\UV4\UV4.exe"))
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--baud-460800", action="store_true",
                        help="Also link enabled ground F103/F407 copies with DC_UART_BAUD=460800u")
    args = parser.parse_args()
    if not 1 <= args.timeout <= 60: parser.error("Per-target UV4 timeout must be 1..60 seconds")
    REPORTS.mkdir(parents=True, exist_ok=True)
    report = {"passed": False, "board_tested": False,
              "checked_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
              "validation": "Real Keil complete compile/link, disabled delivery plus enabled link-only copies; no board execution or HEX",
              "projects": [], "errors": []}
    try:
        check(args.uv4.is_file(), "Keil UV4 executable missing")
        spec = importlib.util.spec_from_file_location("standalone_generator", TEMPLATES / "create_projects.py")
        generator = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(generator)
        generator.generate(verbose=False)
        before = source_hashes()
        board_config_sha = sha(ROOT / "platforms/stm32/common/board_config.h")
        for mode in MODES:
            for role in ROLES:
                report["projects"].append(build_one(args, role, mode))
        if args.baud_460800:
            for role in ("ground_f103", "ground_f407"):
                report["projects"].append(build_one(args, role, "enabled_link_only_460800"))
        report["source_sha256"] = source_hashes()
        check(before == report["source_sha256"], "Sources changed during standalone validation")
        check(board_config_sha == sha(ROOT / "platforms/stm32/common/board_config.h"), "Board config changed during validation")
        report["startup_provenance"] = json.loads((TEMPLATES / "startup_provenance.json").read_text())
        report["baud_460800_checked"] = args.baud_460800
        report["passed"] = all(entry["passed"] for entry in report["projects"]) and len(report["projects"]) == (8 if args.baud_460800 else 6)
    except (ValueError, OSError, ImportError) as exc:
        report["errors"].append(str(exc))
    path = ROOT / "reports/stm32_standalone.json"
    path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    if report["errors"]: print("Standalone validation:", "; ".join(report["errors"]))
    print("Report:", path)
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
