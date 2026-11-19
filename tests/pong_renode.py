"""Check Pong through the shared menu and USART2 in Renode."""

import argparse
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory


def verify(trace):
    """Require the input effects, match results and ANSI colors in the UART log."""
    start = b"MCU Console Games"
    choose = b"Choose difficulty"
    events = (
        start, choose,
        b"You 0:0 CPU\x1b[0m  L1  Easy",
        b"\x1b[08;02H\x1b[1;36m|",
        b"\x1b[13;02H\x1b[1;36m|",
        b"You 5:4 CPU", b"You win!", b"N - next level",
        b"You 0:0 CPU\x1b[0m  L2  Easy", start,
        choose, b"You 0:0 CPU\x1b[0m  L1  Hard", start,
        choose, start,
        choose, b"You 0:0 CPU\x1b[0m  L1  Normal",
        b"You 0:5 CPU", b"CPU wins!", start,
    )
    position = 0
    for event in events:
        found = trace.find(event, position)
        if found < 0:
            raise ValueError(f"missing {event!r} after byte {position}")
        position = found + len(event)

    for color in (b"\x1b[1;32mO", b"\x1b[1;36m|", b"\x1b[1;35m|", b"\x1b[1;33mPong"):
        if color not in trace:
            raise ValueError(f"missing ANSI color {color!r}")
    print(f"Pong USART2: {len(events)} ordered events, 4 ANSI colors, {len(trace)} bytes")


def main():
    """Run one headless Renode session and save the monitor and UART logs."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path, help="integrated games.elf")
    parser.add_argument("--renode", default="renode", help="Renode 1.17.0 executable")
    parser.add_argument("--output", type=Path, default=Path("build/pong-check"))
    args = parser.parse_args()
    if not args.elf.is_file():
        parser.error(f"ELF not found: {args.elf}")

    profile = Path(__file__).resolve().parents[1] / "emulation/stm32f4.resc"
    args.output.mkdir(parents=True, exist_ok=True)
    monitor = args.output / "renode.log"
    uart = args.output / "pong.uart"
    with TemporaryDirectory(prefix="pong-renode-") as directory:
        work = Path(directory)
        elf = work / "games.elf"
        shutil.copyfile(args.elf, elf)
        script = work / "pong.resc"
        profile_lines = profile.read_text(encoding="utf-8").splitlines(keepends=True)
        board_setup = "".join(line for line in profile_lines if not line.startswith("$bin?="))
        script.write_text(f"$bin=@{elf}\n" + board_setup, encoding="utf-8")
        trace = work / "pong.uart"
        command = [
            args.renode, "--disable-gui", "--hide-analyzers", "--plain",
            "--port", "-1", str(script),
            "-e", "sysbus.cpu PerformanceInMips 16",
            "-e", f"sysbus.usart2 CreateFileBackend @{trace} true",
            "-e", 'emulation RunFor "00:00:00.15"',
        ]
        for key in "31Wsnq33Q3Q32Q":
            delay = {"s": 22, "2": 24}.get(key)
            duration = "00:00:00.15" if delay is None else f"00:00:{delay:02d}.00"
            command.extend([
                "-e", f"sysbus.usart2 WriteChar {ord(key)}",
                "-e", f'emulation RunFor "{duration}"',
            ])
        try:
            with monitor.open("w", encoding="utf-8") as log:
                subprocess.run(command + ["-e", "quit"], check=True, stdout=log,
                               stderr=subprocess.STDOUT, timeout=240)
        finally:
            if trace.is_file():
                shutil.copyfile(trace, uart)
        verify(trace.read_bytes())


if __name__ == "__main__":
    main()
