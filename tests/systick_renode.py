"""Check the firmware millisecond clock against one virtual second in Renode."""

import argparse
from pathlib import Path
import re
import shutil
import subprocess
from tempfile import TemporaryDirectory


def main():
    """Run the firmware and compare two reads of its SysTick counter."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--renode", default="renode")
    parser.add_argument("--profile", type=Path,
                        default=Path(__file__).resolve().parents[1] / "emulation/stm32f4.resc")
    args = parser.parse_args()
    symbols = subprocess.check_output(
        ["arm-none-eabi-nm", "--defined-only", str(args.elf)], text=True
    )
    address = re.search(r"^([0-9a-fA-F]+) [bBdD] milliseconds$", symbols, re.MULTILINE)
    if not address:
        raise ValueError("could not locate the firmware milliseconds counter")
    read = f"sysbus ReadDoubleWord 0x{address[1]}"

    with TemporaryDirectory(prefix="mcu-systick-") as directory:
        work = Path(directory)
        elf = work / "games.elf"
        shutil.copyfile(args.elf, elf)
        profile = args.profile.read_text(encoding="utf-8").splitlines(keepends=True)
        setup = "".join(line for line in profile if not line.startswith("$bin?="))
        script = work / "clock.resc"
        script.write_text(f"$bin=@{elf}\n" + setup, encoding="utf-8")
        result = subprocess.run(
            [args.renode, "--console", "--disable-gui", "--hide-analyzers", "--plain", str(script),
             "-e", "sysbus.cpu PerformanceInMips 16",
             "-e", 'emulation RunFor "00:00:00.10"', "-e", read,
             "-e", 'emulation RunFor "00:00:01.00"', "-e", read, "-e", "quit"],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=60,
        )
    ticks = [int(value, 16) for value in re.findall(r"^0x([0-9a-fA-F]{8})$", result.stdout, re.MULTILINE)]
    if result.returncode or len(ticks) != 2:
        raise RuntimeError(f"could not read two complete SysTick samples\n{result.stdout[-3000:]}")
    elapsed = (ticks[1] - ticks[0]) & 0xFFFFFFFF
    if not 999 <= elapsed <= 1001:
        raise ValueError(f"SysTick: expected 1000 ms in one virtual second, got {elapsed}")
    print(f"SysTick PASS: {ticks[0]} -> {ticks[1]} ({elapsed} ms in one virtual second)")


if __name__ == "__main__":
    main()
