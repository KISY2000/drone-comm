"""Regenerate the three disabled standalone MDK communication applications.

Uses the audited deps HAL/CMSIS versions, not CubeMX-generated sources.
Only standalone templates, copied startup files and provenance are written.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
ROLES = (
    ("ground_f103", "f1", "STM32F103C8", "STM32F1xx_DFP@2.4.1", "Cortex-M3", 0x10000, 0x5000),
    ("ground_f407", "f4", "STM32F407ZG", "STM32F4xx_DFP@3.1.1", "Cortex-M4", 0x100000, 0x20000),
    ("air_f407", "f4", "STM32F407ZG", "STM32F4xx_DFP@3.1.1", "Cortex-M4", 0x100000, 0x20000),
)


def child(parent, tag, value=None):
    node = ET.SubElement(parent, tag)
    if value is not None:
        node.text = str(value)
    return node


def rel(path):
    return os.path.relpath(path, HERE).replace("/", "\\")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def generate(verbose=True):
    provenance = {"stack_bytes": 4096, "heap_bytes": 0, "vendor_startup_copies": []}
    for family, version, device in (("f1", "4.3.5", "stm32f103xb"),
                                    ("f4", "2.6.11", "stm32f407xx")):
        upstream = ROOT / f"deps/cmsis-device-{family}-{version}/Source/Templates/arm/startup_{device}.s"
        text = upstream.read_text(encoding="utf-8-sig")
        text, stack_count = re.subn(r"(?m)^(Stack_Size\s+EQU\s+)0x[0-9A-Fa-f]+", r"\g<1>0x00001000", text)
        text, heap_count = re.subn(r"(?m)^(Heap_Size\s+EQU\s+)0x[0-9A-Fa-f]+", r"\g<1>0x00000000", text)
        if (stack_count, heap_count) != (1, 1):
            raise ValueError(f"Unexpected vendor startup format: {upstream}")
        copied = HERE / upstream.name
        copied.write_text(text, encoding="utf-8", newline="\n")
        license_file = ROOT / f"deps/cmsis-device-{family}-{version}" / ("License.md" if family == "f1" else "LICENSE.md")
        license_copy = HERE / f"LICENSE_STM32{family.upper()}.md"
        license_copy.write_bytes(license_file.read_bytes())
        provenance["vendor_startup_copies"].append({
            "upstream": upstream.relative_to(ROOT).as_posix(), "upstream_sha256": sha(upstream),
            "copy": copied.relative_to(ROOT).as_posix(), "copy_sha256": sha(copied),
            "license": license_copy.relative_to(ROOT).as_posix(),
            "changes": ["Stack_Size 0x400 -> 0x1000", "Heap_Size 0x200 -> 0", "line endings normalized to LF"],
        })
    (HERE / "startup_provenance.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")

    for role, family, device, pack, cpu, flash, ram in ROLES:
        hal = ROOT / ("deps/stm32f1xx-hal-driver-1.1.10" if family == "f1" else "deps/stm32f4xx-hal-driver-1.8.5")
        cmsis = ROOT / ("deps/cmsis-device-f1-4.3.5" if family == "f1" else "deps/cmsis-device-f4-2.6.11")
        common = ROOT / "platforms/stm32/common"
        startup = HERE / ("startup_stm32f103xb.s" if family == "f1" else "startup_stm32f407xx.s")
        scatter = HERE / (role + ".sct")
        scatter.write_text(
            f"; Strict link bounds; stack 4096 bytes and heap 0 are in startup.\n"
            f"LR_IROM1 0x08000000 0x{flash:08X} {{\n"
            f"  ER_IROM1 0x08000000 0x{flash:08X} {{\n"
            "    *.o (RESET, +First)\n    *(InRoot$$Sections)\n    .ANY (+RO)\n  }\n"
            f"  RW_IRAM1 0x20000000 0x{ram:08X} {{\n    .ANY (+RW +ZI)\n  }}\n}}\n",
            encoding="utf-8")
        project = ET.Element("Project", {"xmlns:xsi": "http://www.w3.org/2001/XMLSchema-instance",
                                         "xsi:noNamespaceSchemaLocation": "project_projx.xsd"})
        child(project, "SchemaVersion", "2.1")
        child(project, "Header", "### uVision Project, (C) Keil Software")
        target = child(child(project, "Targets"), "Target")
        child(target, "TargetName", role + "_standalone_disabled")
        child(target, "ToolsetNumber", "0x4"); child(target, "ToolsetName", "ARM-ADS")
        child(target, "pCCUsed", "6220000::V6.22::ARMCLANG"); child(target, "uAC6", "1")
        option = child(target, "TargetOption")
        tc = child(option, "TargetCommonOption")
        child(tc, "Device", device); child(tc, "Vendor", "STMicroelectronics")
        child(tc, "PackID", "Keil::" + pack); child(tc, "PackURL", "https://www.keil.com/pack/")
        child(tc, "Cpu", f'IRAM(0x20000000,0x{ram:X}) IROM(0x08000000,0x{flash:X}) CPUTYPE("{cpu}") CLOCK(8000000) ELITTLE')
        output = rel(ROOT / "build/stm32_standalone/default_disabled" / role) + "\\"
        child(tc, "OutputDirectory", output); child(tc, "ListingPath", output)
        child(tc, "OutputName", role + "_disabled")
        for name, value in (("CreateExecutable", 1), ("CreateLib", 0), ("CreateHexFile", 0), ("DebugInformation", 1)):
            child(tc, name, value)
        arm = child(option, "TargetArmAds")
        misc = child(arm, "ArmAdsMisc")
        child(misc, "AdsCpuType", f'"{cpu}"')
        child(misc, "useUlib", 1)
        for name in ("AdsLLst", "AdsLmap", "AdsLcgr", "AdsLsym", "AdsLszi"):
            child(misc, name, 1)
        cads = child(arm, "Cads")
        for name, value in (("Optim", 2), ("wLevel", 2), ("uC99", 1), ("v6Lang", 1), ("v6Lto", 0)):
            child(cads, name, value)
        controls = child(cads, "VariousControls")
        child(controls, "MiscControls", "-std=c99")
        defines = ["USE_HAL_DRIVER", "STM32F103xB" if family == "f1" else "STM32F407xx",
                   "DC_ROLE_" + role.upper(), "DC_BOARD_READY=0", "HSE_VALUE=8000000U"]
        if family == "f1": defines.append("DC_STM32_F1")
        child(controls, "Define", " ".join(defines)); child(controls, "Undefine", "")
        includes = [ROOT / "include", ROOT / "platforms/common", common,
                    ROOT / f"platforms/stm32/{family}", hal / "Inc", cmsis / "Include",
                    ROOT / "deps/CMSIS_5-5.9.0/CMSIS/Core/Include"]
        child(controls, "IncludePath", ";".join(rel(path) for path in includes))
        aads = child(arm, "Aads")
        child(aads, "ClangAsOpt", 1)
        child(aads, "VariousControls")
        ld = child(arm, "LDads")
        for name, value in (("umfTarg", 0), ("useFile", 1), ("noStLib", 0),
                            ("RepFail", 1), ("ScatterFile", rel(scatter)),
                            ("Misc", "--entry=Reset_Handler --strict")):
            child(ld, name, value)
        groups = child(target, "Groups")
        modules = ["", "_uart", "_dma", "_spi", "_gpio", "_rcc", "_rcc_ex", "_cortex", "_flash", "_flash_ex", "_pwr"]
        if family == "f4": modules += ["_rng", "_pwr_ex"]
        source_groups = {
            "startup_and_main": [startup, cmsis / f"Source/Templates/system_stm32{family}xx.c", HERE / "main.c"],
            "shared_protocol": [ROOT / f"src/{name}.c" for name in ("dc_protocol", "dc_node", "dc_radio")],
            "manual_board_and_adapter": [ROOT / "platforms/common/dc_byte_ring.c"] +
                [common / (name + ".c") for name in ("dc_stm32_port", "dc_stm32_app", "main_hooks", "board_peripherals")],
            "vendor_HAL": [hal / f"Src/stm32{family}xx_hal{name}.c" for name in modules],
        }
        for group_name, paths in source_groups.items():
            group = child(groups, "Group"); child(group, "GroupName", group_name)
            files = child(group, "Files")
            for path in paths:
                if not path.is_file(): raise FileNotFoundError(path)
                entry = child(files, "File"); child(entry, "FileName", path.name)
                child(entry, "FileType", 2 if path.suffix == ".s" else 1)
                child(entry, "FilePath", rel(path))
        ET.indent(project)
        ET.ElementTree(project).write(HERE / (role + ".uvprojx"), encoding="utf-8", xml_declaration=True)
        if verbose:
            print("Generated", role, "standalone BOARD_READY=0")


if __name__ == "__main__":
    generate()
