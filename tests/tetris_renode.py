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
GAME_OVER = b"\x1b[2KGame over."
BOARD_PAINT = re.compile(rb"\x1b\[[0-9]+;(?:[2-9]|1[0-9]|2[0-5])H(?:\x1b\[[0-9;]+m)*\[\]")
BOTTOM_CURSOR = b"\x1b[24;40H"
FIELD_BG = b"\x1b[48;2;36;22;56m"
BASE_BG = b"\x1b[48;2;24;15;39m"
I_BG = b"\x1b[48;2;194;217;255m"
GHOST = b"\x1b[20;12H\x1b[38;2;205;170;209m" + FIELD_BG + b"[]\x1b[21;10H[][][]"
QUAD_NOTICE = re.compile(rb"\x1b\[12;28H\x1b\[38;2;255;203;151m(?:\x1b\[48;2;24;15;39m)?4 LINES \+1600")
QUAD_ERASE = re.compile(rb"\x1b\[12;28H(?:\x1b\[[0-9;]+m)*\x1b\[K")
# One group of turns and horizontal moves per piece, followed by hard drop.
QUAD_REPLAY = (
    "D", "WAA", "WAAAAA", "AAA", "WDD", "W", "WWWDDDDD",
    "WWAAA", "WDDDD", "AAAAA", "AA", "WWWDDD", "W", "WDDDDD",
)


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
        DIFFICULTY, b"Difficulty: Easy", MENU,
    )
    if len(screens) != len(labels):
        raise ValueError(f"Expected {len(labels)} screens, received {len(screens)}")
    for index, (screen, label) in enumerate(zip(screens, labels), start=1):
        if label not in screen:
            raise ValueError(f"Screen {index} is missing {label.decode()}")

    easy, hard, normal = (screens[index] for index in (4, 7, 10))
    for game, interval in zip((easy, hard, normal), (850, 350, 600)):
        drop = f"Drop: {interval} ms".encode()
        if b"Score 0 L1 Ln0" not in game or drop not in game or not BOARD_PAINT.search(game):
            raise ValueError(f"Missing starting HUD or ANSI color at {interval} ms")
        if b"Next block:" not in game or I_BG + b"[][][][]" not in game:
            raise ValueError("Missing the opening next-piece preview")
        if FIELD_BG not in game or BASE_BG not in game:
            raise ValueError("Missing retrowave board or canvas background")
        if not game.endswith(RESET):
            raise ValueError("Tetris did not reset the terminal style before returning to menu")
    if b"A/D or Left/Right" not in easy or b"W/Up" not in easy or b"S/Down" not in easy:
        raise ValueError("The game screen is missing the arrow key guide")
    if easy.count(BOTTOM_CURSOR) < 6 or b"Game over." in easy:
        raise ValueError("Easy round should draw the start, gravity and key updates")
    if hard.count(GAME_OVER) != 1 or normal.count(BOTTOM_CURSOR) != 1:
        raise ValueError("Hard game over or clean Normal restart was not observed")
    demo = screens[13]
    if GHOST not in demo.split(BOTTOM_CURSOR, 1)[0] or b"Space" not in demo:
        raise ValueError("The opening frame is missing the ghost or the hard-drop hint")
    if not demo.endswith(RESET):
        raise ValueError("The demo did not reset the terminal style")
    notice = list(QUAD_NOTICE.finditer(demo))
    if b"Score 1600 L1 Ln4" not in demo or len(notice) != 1:
        raise ValueError("The replay did not score one four-line clear")
    quad_frame = demo[:notice[0].start()].rsplit(BOTTOM_CURSOR, 1)[-1]
    if FIELD_BG + b"  " not in quad_frame:
        raise ValueError("The four-line clear did not repaint vacated cells in the field color")
    if not QUAD_ERASE.search(demo, notice[0].end()):
        raise ValueError("The four-line notification did not expire")


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
        send_keys(commands, "ADWS", "00:00:00.06")
        send_keys(commands, "\033[", "00:00:00.03")
        commands.append('emulation RunFor "00:00:00.20"')
        send_keys(commands, "Q", "00:00:00.06")
        send_keys(commands, "13", "00:00:00.10")
        send_keys(commands, "s" * 100, "00:00:00.03")
        send_keys(commands, "Q12q", "00:00:00.12")
        send_keys(commands, "11", "00:00:00.08")
        for moves in QUAD_REPLAY:
            for key in moves:
                interval = "00:00:00.19" if key == "W" else "00:00:00.025"
                send_keys(commands, key, interval)
            send_keys(commands, " ", "00:00:00.04")
        commands.append('emulation RunFor "00:00:01.25"')
        send_keys(commands, "Q", "00:00:00.08")

        args = [renode, "--disable-gui", "--hide-analyzers", "--plain", "--port", "-1", str(scenario)]
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
    print("PASS USART2: play and exit, game over, restart, ghost and four-line hard drop")


if __name__ == "__main__":
    main()
