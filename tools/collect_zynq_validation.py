#!/usr/bin/env python3
"""Archive real Vivado/SDK evidence; reject stale sources and fixture builds.

Run after create_zynq_validation.tcl and create_zynq_bsp.tcl. This collector
does not synthesize, compile, modify a hardware project, or copy binaries.
"""
from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

SOURCES = (
    "include/dc_protocol.h", "include/dc_node.h", "include/dc_link_config.h", "src/dc_protocol.c",
    "src/dc_node.c", "platforms/common/dc_byte_ring.h",
    "platforms/common/dc_byte_ring.c", "platforms/zynq/dc_zynq_port.h",
    "platforms/zynq/dc_zynq_port.c", "platforms/zynq/main_hooks.c",
    "platforms/zynq/board_config.h", "platforms/zynq/standalone_main.c",
    "platforms/zynq/dc_zynq_log.h", "platforms/zynq/dc_zynq_log.c",
)
SCRIPTS = ("tools/create_zynq_validation.tcl", "tools/create_zynq_bsp.tcl")
REFERENCE = "platforms/zynq/reference/system_7020_ps_original.bd"
BOARD_PROFILE = "platforms/zynq/board_profile_7020.json"


def need(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def sha(path: Path) -> str:
    need(path.is_file(), f"Missing file: {path}")
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read(path: Path) -> str:
    need(path.is_file(), f"Missing text evidence: {path}")
    return path.read_text(encoding="utf-8-sig", errors="replace")


def binary_record(path: Path, root: Path) -> dict:
    digest = sha(path)
    need(path.stat().st_size > 0, f"Empty binary: {path}")
    return {"path": path.relative_to(root).as_posix(),
            "sha256": digest, "bytes": path.stat().st_size}


def run(exe: Path, *args: str) -> str:
    need(exe.is_file(), f"Missing SDK tool: {exe}")
    result = subprocess.run([str(exe), *args], capture_output=True,
                            text=True, errors="replace", timeout=30)
    need(result.returncode == 0, f"SDK tool failed: {exe.name}: {result.stderr}")
    return result.stdout


def collect(args: argparse.Namespace, report: dict) -> dict[str, bytes]:
    root = args.root.resolve()
    hardware = root / "build/zynq_uart0_validation"
    workspace = root / f"build/zynq_uart0_sdk_{args.baud}"
    stage = root / f"build/zynq_uart0_sdk_{args.baud}_sources"
    debug = workspace / "dc_zynq_comm/Debug"
    imported = workspace / "dc_zynq_comm/src"
    bsp = workspace / "dc_bsp/ps7_cortexa9_0"
    hdf = hardware / "dc_uart_validation.hdf"
    elf = debug / "dc_zynq_comm.elf"
    source_hashes = report["source_sha256"]
    evidence: dict[str, bytes] = {}

    def save(name: str, path: Path) -> str:
        text = read(path)
        evidence[name] = path.read_bytes()
        return text

    for relative in (*SOURCES, *SCRIPTS, REFERENCE, BOARD_PROFILE,
                     "reports/zynq_reference.json", "tools/collect_zynq_validation.py"):
        source_hashes[relative] = sha(root / relative)
    source_checks = []
    for relative in SOURCES:
        expected = source_hashes[relative]
        name = Path(relative).name
        need(sha(stage / name) == expected, f"Stale staged source: {relative}")
        need(sha(imported / name) == expected, f"Stale SDK imported source: {relative}")
        source_checks.append({"source": relative, "stage_sha256": expected,
                              "sdk_import_sha256": expected})
    report["source_checks"] = source_checks

    reference = json.loads(read(root / "reports/zynq_reference.json"))
    profile = json.loads(read(root / BOARD_PROFILE))
    need(profile["device_family"] == "XC7Z020" and profile["model_7020_confirmed_by_user"] is True,
         "Expected the user-confirmed 7020 board profile")
    need(profile["design_part"] == reference["part"], "Board profile and reference part disagree")
    need(profile["communication_route"] == "uart0_mio14_15"
         and profile["uart_baud_default"] == 115200
         and profile["console"]["uart_output_enabled"] is False,
         "Board profile must describe the current protocol-only UART0 route")
    expected_bd = reference["source_sha256"].lower()
    need(source_hashes[REFERENCE] == expected_bd, "Reference BD differs from audited original")
    external_bd = Path(reference["source"])
    external_checked = external_bd.is_file()
    if external_checked:
        need(sha(external_bd) == expected_bd, "Original external BD was modified")
    report["reference"] = {"sha256": expected_bd, "copy": REFERENCE,
                           "original_external_checked": external_checked,
                           "original_modified": False}

    report["hdf"] = binary_record(hdf, root)
    with zipfile.ZipFile(hdf) as archive:
        need(archive.testzip() is None, "Corrupt hardware export ZIP member")
        need(not any(name.endswith(".bit") for name in archive.namelist()),
             "Compile-only hardware export unexpectedly contains a bitstream")
        hwh = archive.read("system.hwh")
        hwh_xml = ET.fromstring(hwh)
        parameters = {node.attrib["NAME"]: node.attrib["VALUE"]
                      for node in hwh_xml.iter("PARAMETER")
                      if "NAME" in node.attrib and "VALUE" in node.attrib}
        expected = {"PCW_UART0_PERIPHERAL_ENABLE": "1",
                    "PCW_UART0_UART0_IO": "MIO 14 .. 15",
                    "PCW_UART1_PERIPHERAL_ENABLE": "0",
                    "PCW_MIO_14_DIRECTION": "in", "PCW_MIO_15_DIRECTION": "out",
                    "PCW_MIO_14_IOTYPE": "LVCMOS 3.3V", "PCW_MIO_15_IOTYPE": "LVCMOS 3.3V"}
        need(all(parameters.get(key) == value for key, value in expected.items()),
             "HDF does not export the requested physical PS UART routing")
        ports = {node.attrib.get("NAME") for node in hwh_xml.iter("PORT")}
        need(not any(name and "COMM_UART1" in name for name in ports),
             "Disabled UART1 still has EMIO boundary ports")
        evidence["hdf_system.hwh"] = hwh
        evidence["hdf_hwdef.xml"] = archive.read("hwdef.xml")
        report["hdf"]["archive_members"] = archive.namelist()
        report["hdf"]["uart_parameters"] = expected
    sdk_hdf = workspace / "dc_hw/system.hdf"
    need(sha(sdk_hdf) == report["hdf"]["sha256"], "SDK hardware HDF differs from export")
    report["sdk_hdf"] = binary_record(sdk_hdf, root)
    report["elf"] = binary_record(elf, root)
    report["bsp_library"] = binary_record(bsp / "lib/libxil.a", root)
    for relative, output in zip(SCRIPTS, (hdf, elf)):
        need((root / relative).stat().st_mtime_ns <= output.stat().st_mtime_ns,
             f"Build script newer than output: {relative}")

    subdir = save("sdk_src_subdir.mk", debug / "src/subdir.mk")
    makefile = save("sdk_makefile", debug / "makefile")
    for name in ("sources.mk", "objects.mk", "Xilinx.spec"):
        save("sdk_" + name, debug / name)
    save("sdk_lscript.ld", imported / "lscript.ld")
    compile_command = next(line.strip() for line in subdir.splitlines()
                           if line.strip().startswith("arm-none-eabi-gcc "))
    link_command = next(line.strip() for line in makefile.splitlines()
                        if line.strip().startswith("arm-none-eabi-gcc "))
    for text in (subdir, makefile):
        need(not re.search(r"compile_fixture|DC_COMPILE_FIXTURE", text, re.I),
             "Fixture compiler flags found in real SDK make files")
    for flag in ("-mcpu=cortex-a9", "-mfpu=vfpv3", "-mfloat-abi=hard",
                 "-std=c99", "-Wextra", "-Werror",
                 "-I../../dc_bsp/ps7_cortexa9_0/include", f"-DDC_UART_BAUD={args.baud}u"):
        need(flag in compile_command, f"Missing required compiler flag: {flag}")
    need(re.search(r"^#define\s+DC_ZYNQ_BOARD_READY\s+0\s*$",
                   read(imported / "board_config.h"), re.M)
         and "-DDC_ZYNQ_BOARD_READY" not in compile_command,
         "Compile-only BSP application must keep board-ready disabled")
    need("-L../../dc_bsp/ps7_cortexa9_0/lib" in link_command,
         "Link does not use real BSP library")
    report["compiler"] = {"compile_command": compile_command, "link_command": link_command}

    dependencies = []
    c_sources = [Path(relative).stem for relative in SOURCES if relative.endswith(".c")]
    need(sorted(p.stem for p in (debug / "src").glob("*.d")) == sorted(c_sources),
         "SDK dependency files do not match required translation units")
    for stem in c_sources:
        path = debug / "src" / (stem + ".d")
        text = save("dependencies/" + path.name, path)
        need(not re.search(r"compile_fixture|DC_COMPILE_FIXTURE", text, re.I),
             f"Fixture dependency in {path.name}")
        rule = text.replace("\\\n", " ").split("\n\n", 1)[0]
        deps = rule.split(":", 1)[1].split()
        need("../src/" + stem + ".c" in deps, f"Missing actual source dependency: {stem}")
        object_path = debug / "src" / (stem + ".o")
        sha(object_path)
        for token in deps:
            dep = (debug / token).resolve()
            need(dep.is_file(), f"Missing resolved compiler dependency: {token}")
            need(dep.stat().st_mtime_ns <= object_path.stat().st_mtime_ns,
                 f"Stale object {stem}.o: dependency newer: {token}")
        need(object_path.stat().st_mtime_ns <= elf.stat().st_mtime_ns,
             f"ELF older than SDK object: {stem}")
        bsp_deps = [token for token in deps if token.startswith("../../dc_bsp/")]
        if stem in ("dc_zynq_port", "main_hooks", "standalone_main"):
            need(any(token.endswith("/xparameters.h") for token in bsp_deps),
                 f"{stem} did not compile against real generated xparameters.h")
        dependencies.append({"unit": stem + ".c", "real_bsp_dependencies": bsp_deps,
                             "object_sha256": sha(object_path)})
    report["dependencies"] = dependencies
    report["compile_fixture_used"] = False

    # Default BOARD_READY=0 is deliberately retained. A second isolated SDK
    # application links the full enabled path, without programming a target.
    enabled_app = workspace / "dc_zynq_comm_link_only_ready1"
    enabled_debug = enabled_app / "Debug"
    enabled_elf = enabled_debug / "dc_zynq_comm_link_only_ready1.elf"
    enabled_record = binary_record(enabled_elf, root)
    enabled_subdir = save("ready1/sdk_src_subdir.mk", enabled_debug / "src/subdir.mk")
    enabled_make = save("ready1/sdk_makefile", enabled_debug / "makefile")
    need(f"-DDC_UART_BAUD={args.baud}u" in enabled_subdir
         and "-DDC_ZYNQ_BOARD_READY=1" in enabled_subdir,
         "Isolated enabled build lacks the expected baud/board-ready flags")
    need("-L../../dc_bsp/ps7_cortexa9_0/lib" in enabled_make
         and not re.search(r"compile_fixture|DC_COMPILE_FIXTURE", enabled_subdir + enabled_make, re.I),
         "Isolated enabled build must use the real BSP")
    for relative in SOURCES:
        need(sha(enabled_app / "src" / Path(relative).name) == source_hashes[relative],
             f"Stale enabled SDK source: {relative}")
    for stem in c_sources:
        dependency_text = save("ready1/dependencies/" + stem + ".d",
                               enabled_debug / "src" / (stem + ".d"))
        need(not re.search(r"compile_fixture|DC_COMPILE_FIXTURE", dependency_text, re.I),
             "Enabled SDK dependency uses a fixture")
        object_path = enabled_debug / "src" / (stem + ".o")
        rule = dependency_text.replace("\\\n", " ").split("\n\n", 1)[0]
        for token in rule.split(":", 1)[1].split():
            dependency = (enabled_debug / token).resolve()
            need(dependency.is_file() and dependency.stat().st_mtime_ns <= object_path.stat().st_mtime_ns,
                 f"Enabled object has stale dependency: {token}")
        need(object_path.stat().st_mtime_ns <= enabled_elf.stat().st_mtime_ns,
             f"Enabled ELF is older than {stem}.o")
    required_symbols = {"dc_zynq_port_init", "dc_zynq_firmware_start", "dc_zynq_firmware_poll",
                        "dc_node_init", "XUartPs_SetDataFormat", "XScuGic_Connect",
                        "dc_zynq_debug_log", "outbyte", "inbyte"}
    symbol_text = run(args.toolchain / "arm-none-eabi-nm.exe", "--defined-only", str(enabled_elf))
    symbols = set(re.findall(r"^\S+\s+\S\s+(\S+)$", symbol_text, re.M))
    need(required_symbols <= symbols, "Full communication symbols missing from enabled ELF")
    evidence["ready1/elf_symbols.txt"] = symbol_text.encode("utf-8")
    disassembly = run(args.toolchain / "arm-none-eabi-objdump.exe", "-dr",
                      str(enabled_debug / "src/main_hooks.o"))
    for symbol in ("dc_zynq_port_init", "dc_node_init", "dc_zynq_log"):
        need(re.search(r"R_ARM_CALL\s+" + symbol + r"\b", disassembly),
             f"Enabled firmware startup does not call {symbol}")
    evidence["ready1/main_hooks_disassembly.txt"] = disassembly.encode("utf-8")
    enabled_record.update({"passed": True, "board_ready_define": 1,
                           "flashed": False, "delivery_image": False,
                           "required_symbols": sorted(required_symbols),
                           "startup_calls_verified": True})
    report["enabled_link_only"] = enabled_record

    xparameters = save("bsp_xparameters.h", bsp / "include/xparameters.h")
    xparameters_ps = save("bsp_xparameters_ps.h", bsp / "include/xparameters_ps.h")
    save("bsp_xuartps_g.c", bsp / "libsrc/uartps_v3_5/src/xuartps_g.c")
    save("bsp_xscugic_g.c", bsp / "libsrc/scugic_v3_8/src/xscugic_g.c")
    defines = dict(re.findall(r"^#define\s+(\w+)\s+([^\s/]+)",
                              xparameters + "\n" + xparameters_ps, re.M))

    def numeric(name: str) -> int:
        value = defines[name]
        visited = set()
        while value in defines:
            need(value not in visited, f"Cyclic BSP macro: {name}")
            visited.add(value)
            value = defines[value]
        return int(re.sub(r"[uUlL]+$", "", value), 0)

    need(numeric("XPAR_XUARTPS_NUM_INSTANCES") == 1, "Real BSP must contain UART0 only")
    need("STDOUT_BASEADDRESS" not in defines and "STDIN_BASEADDRESS" not in defines,
         "BSP exposes UART stdio despite protocol-exclusive UART0")
    report["uart"] = [
        {"physical": "PS_UART" + str(i), "device_id": numeric(f"XPAR_PS7_UART_{i}_DEVICE_ID"),
         "base_address": f"0x{numeric(f'XPAR_PS7_UART_{i}_BASEADDR'):08X}",
         "irq": numeric(f"XPAR_PS7_UART_{i}_INTR"),
         "clock_hz": numeric(f"XPAR_PS7_UART_{i}_UART_CLK_FREQ_HZ"),
         "route": "MIO14/15", "baud": args.baud}
        for i in range(1)]
    mss = save("bsp_system.mss", workspace / "dc_bsp/system.mss")
    blocks = []
    for kind, body in re.findall(r"BEGIN (\w+)\s+(.*?)\bEND", mss, re.S):
        block = dict(re.findall(r"PARAMETER\s+(\w+)\s*=\s*([^\r\n]+)", body))
        blocks.append({"kind": kind, **{key: value.strip() for key, value in block.items()}})
    os_block = next(block for block in blocks if block["kind"] == "OS")
    # SDK omits parameters equal to the standalone defaults (none) from MSS.
    need(os_block["OS_NAME"] == "standalone" and os_block.get("stdin", "none") == "none"
         and os_block.get("stdout", "none") == "none", "Unexpected BSP OS/console mapping")
    for name in ("inbyte", "outbyte"):
        dummy = save("bsp_" + name + ".c", bsp / "libsrc/standalone_v6_5/src" / (name + ".c"))
        need("XUartPs" not in dummy and "BASEADDRESS" not in dummy,
             "BSP stdio function still accesses a hardware UART")
    report["bsp"] = {"os": os_block, "components": blocks,
                     "stdin_effective": "none", "stdout_effective": "none",
                     "stdio_sink": "application deterministic no-UART inbyte/outbyte overrides",
                     "vendor_warning_note": "SDK 2017.4 generated no-device inbyte has no return statement and outbyte has an unused parameter; vendor BSP warnings are retained. Application defines deterministic sinks and never calls UART stdio.",
                     "cpu_clock_hz": numeric("XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ")}

    synth = save("vivado_synth_runme.log", hardware /
                 "project/dc_uart_validation.runs/synth_1/runme.log")
    warning_lines = re.findall(r"^WARNING:.*$", synth, re.M)
    summaries = re.findall(r"(\d+) Infos, (\d+) Warnings, (\d+) Critical Warnings and (\d+) Errors encountered", synth)
    need(summaries and "synth_design completed successfully" in synth,
         "Missing successful Vivado synthesis summary")
    info, warnings, critical, errors = map(int, summaries[-1])
    need(errors == 0 and critical == 0, "Vivado synthesis has errors/critical warnings")
    need(warnings == len(warning_lines), "Vivado warning summary disagrees with warning lines")
    version = re.search(r"Vivado v([^\s]+)", synth).group(1)
    hardware_validation = save("vivado_validation.txt", hardware / "validation.txt")
    part = re.search(r"^PART=(.*)$", hardware_validation, re.M).group(1).strip()
    need(part == reference["part"], "Synthesized part differs from reference")
    save("vivado_utilization.rpt", hardware / "utilization.rpt")
    for child in ("system_processing_system7_0_0_synth_1", "system_rst_ps7_0_100M_0_synth_1"):
        save("vivado_" + child + ".log", hardware /
             "project/dc_uart_validation.runs" / child / "runme.log")
    need(not list(hardware.rglob("*.bit")), "Unexpected bitstream in compile-only validation")
    physical_xdc = []
    for path in hardware.rglob("*.xdc"):
        text = read(path)
        if "PACKAGE_PIN" in text and "COMM_UART1" in text:
            physical_xdc.append(path.relative_to(root).as_posix())
    need(not physical_xdc, "Physical UART pin XDC exists; re-audit board status")
    implemented = list(hardware.glob("project/*.runs/impl_*/*_routed.dcp"))
    need(not implemented, "Unexpected implementation output in compile-only validation")
    report["vivado"] = {"version": version, "part": part, "synthesis_passed": True,
                        "infos": info, "warnings": warnings, "critical_warnings": critical,
                        "errors": errors, "warning_lines": warning_lines,
                        "physical_uart_xdc": False, "implementation_completed": False,
                        "bitstream_generated": False,
                        "internal_generated_xdc_count": len(list(hardware.rglob("*.xdc")))}

    save("sdk_validation.txt", workspace / "validation.txt")
    sdk_log = save("sdk_SDK.log", workspace / "SDK.log")
    sdk_errors = [line for line in sdk_log.splitlines() if " ERROR " in line]
    size_output = run(args.toolchain / "arm-none-eabi-size.exe", str(elf))
    evidence["elf_size_current.txt"] = size_output.encode("utf-8")
    save("elf_size_build.txt", debug / "dc_zynq_comm.elf.size")
    size_rows = re.findall(r"^\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([0-9a-fA-F]+)\s+", size_output, re.M)
    need(len(size_rows) == 1, "Cannot parse current ELF size")
    text, data, bss, total, hex_total = size_rows[0]
    report["elf"]["sections_bytes"] = dict(zip(("text", "data", "bss", "total"),
                                                 map(int, (text, data, bss, total))))
    header = run(args.toolchain / "arm-none-eabi-readelf.exe", "-h", str(elf))
    need(re.search(r"Class:\s+ELF32", header) and re.search(r"Machine:\s+ARM", header),
         "Expected ARM ELF32 executable")
    evidence["elf_header.txt"] = header.encode("utf-8")
    gcc_version = run(args.toolchain / "arm-none-eabi-gcc.exe", "--version")
    evidence["compiler_version.txt"] = gcc_version.encode("utf-8")
    report["compiler"]["version"] = gcc_version.splitlines()[0]
    report["sdk"] = {"version": "2017.4", "bsp_generated": True, "app_compile_link_passed": True,
                     "log_error_lines": sdk_errors,
                     "log_note": "SDK.log records an XSCT channel-close error and is retained verbatim. The existing real BSP, current sources, dependency files and linked ELF separately establish compile/link success; this report does not claim a zero-error SDK service log."}
    report["board_tested"] = False
    report["validation_scope"] = "Current UART0 PS MIO14/15 synthesis/HDF/BSP build; no PL UART routing or DRC claim"
    report["communication_route"] = "uart0_mio14_15"
    report["uart_baud_default"] = 115200
    report["validated_baud"] = args.baud
    report["console_uart_output_enabled"] = False
    report["physical_board_confirmed"] = False
    report["physical_board_confirmed_scope"] = "Full electrical assembly, bank voltage and wiring have not been measured"
    report["model_7020_confirmed_by_user"] = True
    report["board_profile"] = BOARD_PROFILE
    report["board_ready"] = False
    report["limitations"] = [
        "7020 is confirmed by the user; xc7z020clg400-2 is the audited design part, not a measurement of the physical speed grade or electrical assembly.",
        "This report covers UART0 MIO14/15 with UART1 disabled. No PL UART XDC, implementation, bitstream, flashing or board execution. Old UART1 EMIO evidence is historical only.",
        "UART0 signal quality/baud and real video still require board-level integration; video state is simulated. P5 links 1-3 and 2-4 must be removed.",
        "BSP stdin/stdout are none; application logs only to bounded RAM. Future FSBL and boot code must also avoid UART0 text.",
        "Binary artifacts are hashed and sized only; HDF/ELF/libxil.a are not copied into evidence.",
    ]
    report["passed"] = True
    return evidence


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--toolchain", type=Path, default=Path(
        r"D:\Xilinx\SDK\2017.4\gnu\aarch32\nt\gcc-arm-none-eabi\bin"))
    args = parser.parse_args()
    report = {"passed": False, "source_sha256": {},
              "collected_utc": dt.datetime.now(dt.timezone.utc).isoformat(), "errors": []}
    destination = args.root / "reports/zynq_sdk_validation.json"
    try:
        args.baud = 115200
        primary_evidence = collect(args, report)
        secondary = {"passed": False, "source_sha256": {}}
        args.baud = 460800
        secondary_evidence = collect(args, secondary)
        report["validated_baud_profiles"] = [115200, 460800]
        report["baud_profile_results"] = {"115200": {"passed": True, "elf": report["elf"], "compiler": report["compiler"]}, "460800": secondary}
        evidence = {"115200/" + key: value for key, value in primary_evidence.items()}
        evidence.update({"460800/" + key: value for key, value in secondary_evidence.items()})
        evidence_root = args.root / "reports/zynq_sdk_validation"
        report["evidence_sha256"] = {}
        for relative, data in sorted(evidence.items()):
            path = evidence_root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            report["evidence_sha256"][path.relative_to(args.root).as_posix()] = hashlib.sha256(data).hexdigest()
    except (ValueError, OSError, KeyError, StopIteration, AttributeError,
            subprocess.SubprocessError, zipfile.BadZipFile, ET.ParseError) as exc:
        report["passed"] = False
        report["errors"].append(str(exc))
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"passed": report["passed"], "report": str(destination),
                      "errors": report["errors"]}, ensure_ascii=False))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
