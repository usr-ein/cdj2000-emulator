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


@pytest.mark.parametrize("panel", ["2000", "nxs"])
def test_each_name_is_its_own_bits(driver, panel):
    """No two lamps share a bit, a name or a byte beyond the payload; a
    multi-bit lamp is one field of adjacent bits."""
    names = [line.split()[1:] for line in drive(driver, [f"names {panel}"])]
    assert names
    taken: dict[tuple[int, int], str] = {}
    for byte, mask, name in names:
        byte, mask = int(byte), int(mask, 16)
        assert byte < PAYLOAD and mask
        low = mask & -mask
        assert mask in (low, 3 * low), f"{name}: {mask:#04x} is not one or two adjacent bits"
        for bit in range(8):
            if mask & (1 << bit):
                assert (byte, bit) not in taken, f"{name} and {taken[(byte, bit)]}"
                taken[(byte, bit)] = name
    assert len({name for _, _, name in names}) == len(names)


def test_the_nxs_names_its_own_lamps(driver):
    """The NXS lays the lamps out anew: PLAY/PAUSE is 2.6 there, and 1.5,
    the CDJ-2000's PLAY/PAUSE, is a bit nothing has been seen to light."""
    lit = frame(b1=0x10 | 0x20, b2=0x40 | 0x20, b3=0x40, b4=0x30 | 0x04,
                b5=0x01, b6=0xc0, b13=0x0c | 0x01)
    out = drive(driver, ["init nxs track", f"frame 0 {lit}", "summary",
                         "init 2000 track", f"frame 0 {frame(b1=0x20)}", "summary",
                         "init nxs track", f"frame 0 {frame(b1=0x20)}", "summary"])
    assert summaries(out) == [
        "QUANTIZE:on,CUE:on,PLAY_PAUSE:on,SOURCE_REKORDBOX:1,SOURCE_SD:1,"
        "SOURCE_USB:3,BROWSE:1,LOOP_MODE:3,SYNC:1,MASTER:3",
        "PLAY_PAUSE:on",
        "-"]


def test_the_nxs_master_lamp_moves_from_one_deck_to_the_other(driver):
    """MASTER as two NXSs on a link showed it: 0x0d (master) and 0x05."""
    out = drive(driver, ["init nxs log", f"frame 0 {frame(b13=0x0d)}", "summary",
                         f"frame 5000 {frame(b13=0x05)}", "summary"])
    assert summaries(out) == ["SYNC:1,MASTER:3", "SYNC:1,MASTER:1"]
    assert "cdj2000-lamp t=5.000 MASTER 1" in out


def test_the_nxs_jog_display_positions_are_not_lamps(driver):
    """Bytes 7, 9 and 10 move with the playhead; they are not followed even
    when every unnamed bit is."""
    out = drive(driver, ["init nxs all", f"frame 0 {frame()}",
                         f"frame 100 {frame(b7=0x4c, b9=0x2a, b10=0x13)}", "summary"])
    assert out == ["changed", "summary -"]


def test_all_names_the_unnamed_bits_by_byte_and_bit(driver):
    """2.4 is a bit of the CDJ-2000's lamp bytes that nothing named."""
    out = drive(driver, ["init 2000 all", f"frame 0 {frame()}",
                         f"frame 1000 {frame(b2=0x10)}", "summary"])
    assert "cdj2000-lamp t=1.000 2.4 on" in out
    assert summaries(out) == ["2.4:on"]
    named = drive(driver, ["init 2000 track", f"frame 0 {frame(b2=0x10)}", "summary"])
    assert summaries(named) == ["-"]
