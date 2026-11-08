#!/usr/bin/env python3
"""Compile the actual keyboard-mode source against a fake Game Boy link."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
main = (root / "Arduinoboy/Arduinoboy.ino").read_text()
start = main.index("byte keyboardNotes[]")
end = main.index("byte keyboardCommands[12];", start) + len("byte keyboardCommands[12];")
with tempfile.TemporaryDirectory(prefix="arduinoboy-keyboard-") as directory:
    temp = Path(directory)
    (temp / "keyboard_settings.inc").write_text(main[start:end])
    subprocess.run([
        os.environ.get("CXX", "c++"), "-std=c++11", "-Wall", "-Wextra", "-Werror",
        "-I", str(temp), str(root / "tests/keyboard_test.cpp"),
        "-o", str(temp / "keyboard_test"),
    ], check=True)
    subprocess.run([str(temp / "keyboard_test")], check=True)
