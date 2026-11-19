"""Verify the Tetris menu, play, result, and restart over STM32F4 USART2."""

import argparse
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


CLEAR = b"\x1b[2J\x1b[H"
RESET = b"\x1b[0m"
MENU = b"MCU Console Games"
DIFFICULTY = b"Choose difficulty"
GAME_OVER = b"\x1b[31mGame over.\x1b[0m  Q - menu"
BOARD_PAINT = re.compile(rb"\x1b\[[0-9]+;[0-9]+H\x1b\[(?:4[1-7]|10[0-7])m")
BOTTOM_CURSOR = b"\x1b[24;1H"


def send_keys(commands: list[str], keys: str, interval: str) -> None:
    """Append UART input with time for the firmware to process each byte."""
    for key in keys:
        commands.extend((f"sysbus.usart2 WriteChar {ord(key)}",
                         f'emulation RunFor "{interval}"'))


def check_uart(data: bytes) -> None:
    """Check the ordered screens and key evidence from the game frames."""
    screens = [frame for frame in data.split(CLEAR)[1:] if frame.replace(RESET, b"").strip()]
    labels = (
        MENU, DIFFICULTY, MENU,
        DIFFICULTY, b"Difficulty: Easy", MENU,
        DIFFICULTY, b"Difficulty: Hard", MENU,
        DIFFICULTY, b"Difficulty: Normal", MENU,
    )
    if len(screens) != len(labels):
        raise ValueError(f"Expected {len(labels)} screens, received {len(screens)}")
    for index, (screen, label) in enumerate(zip(screens, labels), start=1):
        if label not in screen:
            raise ValueError(f"Screen {index} is missing {label.decode()}")

    easy, hard, normal = (screens[index] for index in (4, 7, 10))
    for game, interval in zip((easy, hard, normal), (850, 350, 600)):
        hud = f"Score: 0 | Level: 1 | Lines: 0 | Drop: {interval} ms".encode()
        if hud not in game or not BOARD_PAINT.search(game):
            raise ValueError(f"Missing starting HUD or ANSI color at {interval} ms")
    if b"Rotate  W      or Up" not in easy or b"Drop    S      or Down" not in easy:
        raise ValueError("The game screen is missing the arrow key guide")
    if easy.count(BOTTOM_CURSOR) < 6 or b"Game over." in easy:
        raise ValueError("Easy round should draw the start, gravity and key updates")
    if hard.count(GAME_OVER) != 1 or normal.count(BOTTOM_CURSOR) != 1:
        raise ValueError("Hard game over or clean Normal restart was not observed")


def run_renode(renode: str, elf: Path, profile: Path) -> bytes:
    """Load the supplied firmware and collect one deterministic UART session."""
    with tempfile.TemporaryDirectory(prefix="tetris-renode-") as directory:
        work = Path(directory)
        local_elf = work / "games.elf"
        uart = work / "tetris.uart"
        scenario = work / "tetris.resc"
        shutil.copyfile(elf, local_elf)
        scenario.write_text(f"$bin=@{local_elf}\n" + profile.read_text(encoding="utf-8"),
                            encoding="utf-8")

        commands = [f"sysbus.usart2 CreateFileBackend @{uart} true",
                    'emulation RunFor "00:00:00.20"']
        send_keys(commands, "1Q11", "00:00:00.10")
        commands.append('emulation RunFor "00:00:00.90"')
        send_keys(commands, "\033[D\033[C\033[A\033[B", "00:00:00.03")
        send_keys(commands, "ADWSQ", "00:00:00.06")
        send_keys(commands, "13", "00:00:00.10")
        send_keys(commands, "s" * 100, "00:00:00.03")
        send_keys(commands, "Q12q", "00:00:00.12")

        args = [renode, "--disable-gui", "--hide-analyzers", "--plain", str(scenario)]
        for command in (*commands, "quit"):
            args.extend(("-e", command))
        result = subprocess.run(args, capture_output=True, text=True, timeout=120, check=False)
        if result.returncode or not uart.is_file():
            raise RuntimeError(f"Renode exited with status {result.returncode}\n"
                               f"{result.stdout[-1500:]}{result.stderr[-1500:]}")
        return uart.read_bytes()


def main() -> None:
    """Run the smoke check against a built firmware supplied by the caller."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", type=Path, default=Path("build/games.elf"))
    parser.add_argument("--renode", default="renode", help="Renode 1.17.0 executable")
    parser.add_argument("--uart-log", type=Path, help="optional raw USART2 capture")
    args = parser.parse_args()
    profile = Path(__file__).resolve().parents[1] / "emulation" / "stm32f4.resc"
    try:
        data = run_renode(args.renode, args.elf, profile)
        if args.uart_log:
            args.uart_log.write_bytes(data)
        check_uart(data)
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        parser.exit(1, f"Tetris USART2 check failed: {error}\n")
    print("PASS USART2: Easy play and exit; Hard game over; clean restart on Normal")


if __name__ == "__main__":
    main()
