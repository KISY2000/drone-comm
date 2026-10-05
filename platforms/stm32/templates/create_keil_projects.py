"""Regenerate uVision static-library templates, with real vendored dependencies."""
from pathlib import Path
import os
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]


def child(parent, tag, value=None):
    x = ET.SubElement(parent, tag)
    if value is not None: x.text = str(value)
    return x


def relative(path):
    return os.path.relpath(path, HERE).replace("/", "\\")


for family, role, device, pack, cpu, memory in [
    ("f1", "ground_f103", "STM32F103C8", "STM32F1xx_DFP@2.4.1", "Cortex-M3", "IRAM(0x20000000,0x5000) IROM(0x08000000,0x10000)"),
    ("f4", "ground_f407", "STM32F407ZG", "STM32F4xx_DFP@3.1.1", "Cortex-M4", "IRAM(0x20000000,0x20000) IROM(0x08000000,0x100000)"),
    ("f4", "air_f407", "STM32F407ZG", "STM32F4xx_DFP@3.1.1", "Cortex-M4", "IRAM(0x20000000,0x20000) IROM(0x08000000,0x100000)")]:
    hal = next((ROOT / "deps").glob(f"stm32{family}xx-hal-driver-*"))
    cmsis = next((ROOT / "deps").glob(f"cmsis-device-{family}-*"))
    common = ROOT / "platforms/stm32/common"
    project = ET.Element("Project", {"xmlns:xsi": "http://www.w3.org/2001/XMLSchema-instance",
                                     "xsi:noNamespaceSchemaLocation": "project_projx.xsd"})
    child(project, "SchemaVersion", "2.1")
    child(project, "Header", "### uVision Project, (C) Keil Software")
    target = child(child(project, "Targets"), "Target")
    child(target, "TargetName", role + "_comm_library")
    child(target, "ToolsetNumber", "0x4")
    child(target, "ToolsetName", "ARM-ADS")
    child(target, "pCCUsed", "6220000::V6.22::ARMCLANG")
    child(target, "uAC6", "1")
    opt = child(target, "TargetOption")
    tc = child(opt, "TargetCommonOption")
    child(tc, "Device", device); child(tc, "Vendor", "STMicroelectronics")
    child(tc, "PackID", "Keil::" + pack)
    child(tc, "PackURL", "https://www.keil.com/pack/")
    child(tc, "Cpu", memory + f' CPUTYPE("{cpu}") CLOCK(8000000) ELITTLE')
    child(tc, "OutputDirectory", relative(ROOT / "build/keil" / role) + "\\")
    child(tc, "OutputName", role + "_comm")
    child(tc, "CreateExecutable", "0"); child(tc, "CreateLib", "1")
    child(tc, "CreateHexFile", "0"); child(tc, "DebugInformation", "1")
    child(tc, "ListingPath", relative(ROOT / "build/keil" / role) + "\\")
    arm = child(opt, "TargetArmAds")
    misc = child(arm, "ArmAdsMisc")
    child(misc, "AdsCpuType", f'"{cpu}"')
    cad = child(arm, "Cads")
    child(cad, "Optim", "1"); child(cad, "wLevel", "2")
    child(cad, "uC99", "1"); child(cad, "v6Lang", "1")
    child(cad, "VariousControls")
    vc = cad.find("VariousControls")
    defines = ["USE_HAL_DRIVER", "STM32F103xB" if family == "f1" else "STM32F407xx", "DC_ROLE_" + role.upper()]
    if family == "f1": defines.append("DC_STM32_F1")
    child(vc, "MiscControls", "-std=c99")
    child(vc, "Define", " ".join(defines)); child(vc, "Undefine", "")
    includes = [ROOT / "include", ROOT / "platforms/common", common, ROOT / f"platforms/stm32/{family}",
                hal / "Inc", cmsis / "Include", ROOT / "deps/CMSIS_5-5.9.0/CMSIS/Core/Include"]
    child(vc, "IncludePath", ";".join(relative(x) for x in includes))
    groups = child(target, "Groups")
    sources = {"shared": [ROOT / "src/dc_protocol.c", ROOT / "src/dc_node.c", ROOT / "src/dc_radio.c"],
               "adapter": [ROOT / "platforms/common/dc_byte_ring.c", common / "dc_stm32_port.c", common / "dc_stm32_app.c",
                           common / "main_hooks.c", common / "board_peripherals.c"],
               "vendor_HAL": []}
    modules = ["", "_uart", "_dma", "_spi", "_gpio", "_rcc", "_rcc_ex", "_cortex"]
    if family == "f4": modules += ["_rng", "_pwr", "_pwr_ex"]
    sources["vendor_HAL"] = [hal / f"Src/stm32{family}xx_hal{m}.c" for m in modules]
    for group_name, paths in sources.items():
        g = child(groups, "Group"); child(g, "GroupName", group_name); files = child(g, "Files")
        for path in paths:
            f = child(files, "File"); child(f, "FileName", path.name)
            child(f, "FileType", "1"); child(f, "FilePath", relative(path))
    ET.indent(project)
    ET.ElementTree(project).write(HERE / f"{role}.uvprojx", encoding="utf-8", xml_declaration=True)
    print("Generated", HERE / f"{role}.uvprojx")
