"""Compile genuine HAL and SDK headers/sources; produce objects, never flash images.

python tools/check_platforms.py [--sdk D:/Xilinx/SDK/2017.4] [--armclang path]
Missing HDF => checked-in, explicitly compile-only BSP parameter fixture. No stubs.
"""
from pathlib import Path
import argparse
from datetime import datetime, timezone, timedelta
import hashlib
import json
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(cmd):
    p = subprocess.run([str(x) for x in cmd], capture_output=True, text=True)
    return {"command": [str(x) for x in cmd], "returncode": p.returncode,
            "output": (p.stdout + p.stderr).strip()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", default="D:/Xilinx/SDK/2017.4")
    parser.add_argument("--armclang", default="C:/Users/Li/AppData/Local/Keil_v5/ARM/ARMCLANG/bin/armclang.exe")
    args = parser.parse_args()
    sdk = Path(args.sdk)
    gcc = sdk / "gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe"
    out = ROOT / "build/platform_checks"
    out.mkdir(parents=True, exist_ok=True)
    results = {"validation": "compile-only; no link, no HDF, no board acceptance", "checks": [],
               "tested_at": datetime.now(timezone(timedelta(hours=8))).isoformat()}
    if not gcc.exists():
        print("SDK ARM compiler missing:", gcc)
        return 2
    results["gcc_version"] = run([gcc, "--version"])
    armclang = Path(args.armclang)
    if armclang.exists():
        results["armclang_version"] = run([armclang, "--version"])
        probe = ROOT / "platforms/common/dc_byte_ring.c"
        results["keil_compile_probe"] = run([armclang, "--target=arm-arm-none-eabi", "-mcpu=cortex-m3", "-std=c99",
                        "-c", probe, "-o", out / "keil_probe.o"])
    common = ROOT / "platforms/stm32/common"
    own_sources = [ROOT / "platforms/common/dc_byte_ring.c", common / "dc_stm32_port.c",
                   common / "dc_stm32_app.c", common / "main_hooks.c", common / "board_peripherals.c",
                   ROOT / "src/dc_protocol.c", ROOT / "src/dc_node.c", ROOT / "src/dc_radio.c"]
    for family, role, cpu, device in [("f1", "GROUND_F103", "cortex-m3", "STM32F103xB"),
                                      ("f4", "GROUND_F407", "cortex-m4", "STM32F407xx"),
                                      ("f4", "AIR_F407", "cortex-m4", "STM32F407xx")]:
        hal = next((ROOT / "deps").glob(f"stm32{family}xx-hal-driver-*"))
        cmsis = next((ROOT / "deps").glob(f"cmsis-device-{family}-*"))
        includes = [ROOT / "include", ROOT / "platforms/common", common, ROOT / f"platforms/stm32/{family}",
                    hal / "Inc", cmsis / "Include", ROOT / "deps/CMSIS_5-5.9.0/CMSIS/Core/Include"]
        opts = [f"-mcpu={cpu}", "-mthumb", "-std=c99", "-ffreestanding", "-Wall", "-Wextra", "-Werror",
                "-DUSE_HAL_DRIVER", f"-D{device}", f"-DDC_ROLE_{role}"]
        if family == "f1": opts.append("-DDC_STM32_F1")
        sources = list(own_sources)
        # Compile the actual external driver implementation as well, not just adapters.
        if role != "AIR_F407":
            modules = ["", "_uart", "_dma", "_spi", "_gpio", "_rcc", "_rcc_ex", "_cortex"]
            if family == "f4": modules.append("_rng")
            sources += [hal / f"Src/stm32{family}xx_hal{m}.c" for m in modules]
        for src in sources:
            obj = out / f"{role.lower()}_{src.stem}.o"
            r = run([gcc, *opts, *[f"-I{x}" for x in includes], "-c", src, "-o", obj])
            r.update(target=role, source=str(src.relative_to(ROOT)))
            results["checks"].append(r)
            print(f"{'PASS' if r['returncode'] == 0 else 'FAIL'} {role} {src.name}")
            if r["returncode"]: print(r["output"])
            if armclang.exists():
                ar = run([armclang, "--target=arm-arm-none-eabi", *opts, *[f"-I{x}" for x in includes],
                          "-c", src, "-o", out / f"keil_{role.lower()}_{src.stem}.o"])
                ar.update(target=f"KEIL 6.22 {role}", source=str(src.relative_to(ROOT)))
                results["checks"].append(ar)
                print(f"{'PASS' if ar['returncode'] == 0 else 'FAIL'} KEIL {role} {src.name}")
                if ar["returncode"]: print(ar["output"])
    emb = sdk / "data/embeddedsw"
    standalone = emb / "lib/bsp/standalone_v6_5/src"
    includes = [ROOT / "include", ROOT / "platforms/common", ROOT / "platforms/zynq/compile_fixture",
                ROOT / "platforms/zynq", standalone / "common", standalone / "arm/common",
                standalone / "arm/cortexa9", standalone / "arm/common/gcc", emb / "XilinxProcessorIPLib/drivers/uartps_v3_5/src",
                emb / "XilinxProcessorIPLib/drivers/scugic_v3_8/src"]
    for src in [ROOT / "platforms/common/dc_byte_ring.c", ROOT / "platforms/zynq/dc_zynq_port.c",
                ROOT / "platforms/zynq/dc_zynq_log.c", ROOT / "platforms/zynq/main_hooks.c",
                ROOT / "src/dc_protocol.c", ROOT / "src/dc_node.c"]:
        r = run([gcc, "-mcpu=cortex-a9", "-mfpu=vfpv3", "-mfloat-abi=softfp", "-std=c99", "-ffreestanding",
                 "-Wall", "-Wextra", "-Werror", "-DDC_COMPILE_FIXTURE", *[f"-I{x}" for x in includes],
                 "-c", src, "-o", out / f"zynq_{src.stem}.o"])
        r.update(target="ZYNQ SDK 2017.4 real headers / compile-only generated-parameter fixture",
                 source=str(src.relative_to(ROOT)))
        results["checks"].append(r)
        print(f"{'PASS' if r['returncode'] == 0 else 'FAIL'} ZYNQ {src.name}")
        if r["returncode"]: print(r["output"])
    # A regenerated BSP must never silently put console traffic back onto a
    # communication UART. Confirm both forbidden SDK macros fail compilation.
    guard_source = out / "console_guard.c"
    guard_source.write_text('#include "board_config.h"\nint dc_console_guard_probe;\n', encoding="ascii")
    results["console_isolation_guards"] = []
    for macro in ("STDOUT_BASEADDRESS", "STDIN_BASEADDRESS"):
        guard = run([gcc, "-mcpu=cortex-a9", "-std=c99", "-DDC_COMPILE_FIXTURE",
                     f"-D{macro}=0xE0000000U", *[f"-I{x}" for x in includes],
                     "-c", guard_source, "-o", out / "forbidden_console.o"])
        guard["macro"] = macro
        guard["passed"] = guard["returncode"] != 0 and "Communication BSP requires stdin=none and stdout=none" in guard["output"]
        results["console_isolation_guards"].append(guard)
        print(f"{'PASS' if guard['passed'] else 'FAIL'} console guard rejects {macro}")
    keil_probe = results.get("keil_compile_probe")
    results["passed"] = (all(r["returncode"] == 0 for r in results["checks"])
                         and (keil_probe is None or keil_probe["returncode"] == 0)
                         and all(g["passed"] for g in results["console_isolation_guards"]))
    evidence = list((ROOT / "platforms").rglob("*.c")) + list((ROOT / "platforms").rglob("*.h"))
    evidence += list((ROOT / "include").glob("*.h")) + list((ROOT / "src").glob("*.c"))
    results["source_sha256"] = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                                for p in evidence}
    (out / "results.json").write_text(json.dumps(results, ensure_ascii=False, indent=2), encoding="utf-8")
    print("Report:", out / "results.json")
    return 0 if results["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
