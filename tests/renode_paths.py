"""Format paths for the Renode monitor tokenizer."""

from pathlib import Path


def monitor_path(path: Path) -> str:
    """Return an absolute path token with spaces escaped for the monitor."""
    escaped = path.absolute().as_posix().replace(" ", "\\ ")
    return "@" + escaped
