"""Verify the common launcher, all three games and resume over USART2."""

import argparse
import re
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory

from renode_paths import monitor_path


# A launcher frame begins with this exact reset, background and clear.
LAUNCHER = b"\x1b[0m\x1b[48;2;24;15;39m\x1b[2J\x1b[H"


def verify(trace, output):
    """Check ordered state transitions and save the first two launcher frames."""
    frames = trace.split(LAUNCHER)[1:]
    labels = (
        (b"Tetris", b"TETRIS", b"Difficulty: Easy"),
        (b"Minesweeper", b"MINESWEEPER", b"L1/3  Easy  Classic"),
        (b"Minesweeper", b"U continue", b"L1/3  Easy  Classic"),
        (b"Pong", b"PONG", b"L1  Hard"),
        (b"Tetris", b"TETRIS", b"Difficulty: Normal"),
        (b"Pong", None, None),
    )
    if len(frames) != 2 * len(labels) + 1:
        raise ValueError(f"expected 13 launcher frames, found {len(frames)}")
    main = frames[0]
    for marker in (b"MCU CONSOLE GAMES", b"Choose a game", b"Tetris", b"Minesweeper",
                   b"Pong", b"1-3 Select game", b"48;2;36;22;56m",
                   b"48;2;112;63;162m", b"48;2;220;84;155m", b"48;2;255;172;123m"):
        if marker not in main:
            raise ValueError(f"main menu missing {marker!r}")
    if any(frame != main for frame in frames[::2]):
        raise ValueError("a returned main menu differs from the startup menu")
    for frame, (game, title, state) in zip(frames[1::2], labels):
        difficulty, separator, game_frame = frame.partition(b"Q Back\x1b[0m")
        for marker in (b"Choose difficulty", game, b"Easy", b"Normal", b"Hard"):
            if marker not in difficulty:
                raise ValueError(f"{game!r}: difficulty screen missing {marker!r}")
        if not separator:
            raise ValueError(f"{game!r}: difficulty screen missing Q Back")
        if title is None:
            if game_frame:
                raise ValueError("cancelling difficulty started a game")
            continue
        if not game_frame.startswith(b"\x1b[0m\x1b[2J\x1b[H"):
            raise ValueError(f"{game!r}: expected an ANSI reset and full clear before the game draws")
        if title not in game_frame or state not in game_frame:
            raise ValueError(f"{game!r}: missing the real game screen or expected state")
    opened = [re.findall(rb"Opened ([1-9]\d*/\d+)", frames[i]) for i in (3, 5)]
    if not all(opened) or opened[0][-1] != opened[1][-1] or b"Cell A1" not in frames[5]:
        raise ValueError("Minesweeper did not restore the opened cells and cursor")
    (output / "main-menu.uart").write_bytes(LAUNCHER + main)
    footer = b"Q Back\x1b[0m"
    (output / "difficulty.uart").write_bytes(LAUNCHER + frames[1].partition(footer)[0] + footer)
    print("USART2 PASS: 13 launcher frames, 7 identical main menus, "
          "3 games, Tetris restart, Minesweeper resume and difficulty cancel")


def main():
    """Run a single headless STM32F407 session and retain its UART evidence."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--renode", default="renode")
    parser.add_argument("--output", type=Path, default=Path("build/menu-check"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    profile = Path(__file__).resolve().parents[1] / "emulation/stm32f4.resc"
    with TemporaryDirectory(prefix="mcu-games-") as directory:
        work = Path(directory)
        elf = work / "games.elf"
        shutil.copyfile(args.elf, elf)
        lines = profile.read_text(encoding="utf-8").splitlines(keepends=True)
        setup = "".join(line for line in lines if not line.startswith("$bin?="))
        script = work / "games.resc"
        script.write_text(f"$bin={monitor_path(elf)}\n" + setup, encoding="utf-8")
        trace = work / "menu.uart"
        command = [
            args.renode, "--disable-gui", "--hide-analyzers", "--plain", "--port", "-1",
            "-e", f"include {monitor_path(script)}",
            "-e", "sysbus.cpu PerformanceInMips 16",
            "-e", f"sysbus.usart2 CreateFileBackend {monitor_path(trace)} true",
            "-e", 'emulation RunFor "00:00:00.35"',
        ]
        # Open all games, make a first Minesweeper move, save, and resume via U.
        for key in "11Aq21 q22Uq33Wq12q3Q":
            command.extend([
                "-e", f"sysbus.usart2 WriteChar {ord(key)}",
                "-e", 'emulation RunFor "00:00:00.35"',
            ])
        try:
            with (args.output / "renode.log").open("w", encoding="utf-8") as log:
                subprocess.run(command + ["-e", "quit"], check=True, stdout=log,
                               stderr=subprocess.STDOUT, timeout=120)
        finally:
            if trace.is_file():
                shutil.copyfile(trace, args.output / "menu.uart")
        verify(trace.read_bytes(), args.output)


if __name__ == "__main__":
    main()
