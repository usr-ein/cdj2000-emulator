"""The panel lamps, named from the frame MAIN sends the panel board.

`emulator/qemu/cdj2000_lamps.c` is compiled here with a small driver
(tests/cstub/panel-lamps.c) and fed frames built from the tables below, so
what `state` reports for a frame -- the names, the two-bit levels, a blink --
is checked without a machine and without any firmware.  Which bit is which
lamp was measured on the machines (see the tables' comments); this checks
that the decoding says what the tables say.
"""
# SPDX-License-Identifier: GPL-2.0-or-later

from __future__ import annotations

import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[1]
PAYLOAD = 22


@pytest.fixture(scope="session")
def driver(tmp_path_factory) -> Path:
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("requires a C compiler")
    binary = tmp_path_factory.mktemp("lamps") / "panel-lamps"
    subprocess.run([
        cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-I", str(ROOT / "emulator/qemu"),
        str(ROOT / "tests/cstub/panel-lamps.c"),
        str(ROOT / "emulator/qemu/cdj2000_lamps.c"),
        "-o", str(binary),
    ], check=True)
    return binary


def drive(driver: Path, lines: list[str]) -> list[str]:
    done = subprocess.run([str(driver)], input="\n".join(lines) + "\n",
                          capture_output=True, text=True, timeout=10)
    assert done.returncode == 0, done.stderr
    return done.stdout.splitlines()


def frame(**bits: int) -> str:
    """A payload with bits set: frame(b1=0x20, b3=0x0c) sets 0x20 in byte 1..."""
    payload = bytearray(PAYLOAD)
    for name, value in bits.items():
        payload[int(name[1:])] |= value
    return payload.hex()


def summaries(output: list[str]) -> list[str]:
    return [line[len("summary "):] for line in output if line.startswith("summary ")]


def test_the_mode_follows_the_environment(driver):
    assert drive(driver, ["mode - -", "mode - 5984", "mode 0 5984", "mode 1 -",
                          "mode all -", "mode 1 5984"]) == [
        "mode off", "mode track", "mode track", "mode log", "mode all", "mode log"]


def test_dark_lamps_are_left_out_and_none_lit_is_a_dash(driver):
    assert summaries(drive(driver, ["init 2000 track", f"frame 0 {frame()}",
                                    "summary"])) == ["-"]


def test_a_lamp_lights_and_goes_dark(driver):
    out = drive(driver, ["init 2000 track",
                         f"frame 0 {frame()}",
                         f"frame 2000 {frame(b1=0x20)}", "summary",
                         f"frame 4000 {frame()}", "summary"])
    assert summaries(out) == ["PLAY_PAUSE:on", "-"]


def test_a_two_bit_lamp_reports_its_level(driver):
    out = drive(driver, ["init 2000 track",
                         f"frame 0 {frame(b3=0x04)}", "summary",
                         f"frame 2000 {frame(b3=0x0c)}", "summary"])
    assert summaries(out) == ["SOURCE_SD:1", "SOURCE_SD:3"]


def test_a_lamp_that_toggles_twice_within_the_window_is_blinking(driver):
    """Two toggles within 1.2 s are a blink, reported once rather than per
    toggle; held for longer than that, it is a lamp that is simply on."""
    lines = ["init 2000 log", f"frame 0 {frame()}"]
    for step in range(8):
        lines.append(f"frame {1000 + 300 * step} {frame(b1=0x10 if step % 2 == 0 else 0)}")
        lines.append("summary")
    lines += [f"frame 4000 {frame(b1=0x10)}", f"frame 6000 {frame(b1=0x10)}", "summary"]
    out = drive(driver, lines)
    assert summaries(out)[0] == "CUE:on"
    assert set(summaries(out)[1:8]) == {"CUE:blink"}
    assert summaries(out)[-1] == "CUE:on"
    logged = [line for line in out if line.startswith("cdj2000-lamp")]
    assert logged == ["cdj2000-lamp t=1.000 CUE on",
                      "cdj2000-lamp t=1.300 CUE blink 300 ms",
                      "cdj2000-lamp t=6.000 CUE on"]


def test_tracking_alone_logs_nothing(driver):
    out = drive(driver, ["init 2000 track", f"frame 0 {frame()}",
                         f"frame 1000 {frame(b1=0x10)}"])
    assert out == ["changed", "changed"]


def test_all_names_the_unnamed_bits_by_byte_and_bit(driver):
    """2.4 is a bit of the CDJ-2000's lamp bytes that nothing named."""
    out = drive(driver, ["init 2000 all", f"frame 0 {frame()}",
                         f"frame 1000 {frame(b2=0x10)}", "summary"])
    assert "cdj2000-lamp t=1.000 2.4 on" in out
    assert summaries(out) == ["2.4:on"]
    named = drive(driver, ["init 2000 track", f"frame 0 {frame(b2=0x10)}", "summary"])
    assert summaries(named) == ["-"]
