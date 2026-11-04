"""
A corrupt division index is rejected at open instead of read out of bounds.

The division index near the end of an .msz is a run of length-prefixed arrays
(per-spectrum positions, scan numbers, MS levels). Every stored length is now
checked against the start of the footer, and the per-spectrum tables must
agree, so a corrupted length raises MSCompressError instead of crashing.
"""

import shutil
import struct

import pytest

from mscompress import MSCompressError, read

FOOTER_SIZE = 88  # sizeof(footer_t)
DIVISIONS_POS_OFFSET = 6 * 8  # 7th uint64 in the footer
N_DIVISIONS_OFFSET = 72


def _division_layout(data: bytes):
    """Byte offsets of each division's length fields, as the writer laid them out."""
    footer = len(data) - FOOTER_SIZE
    pos = struct.unpack_from("<Q", data, footer + DIVISIONS_POS_OFFSET)[0]
    n_div = struct.unpack_from("<i", data, footer + N_DIVISIONS_OFFSET)[0]
    divisions = []
    for _ in range(n_div):
        div = {"dp": []}
        for _ in range(4):  # spectra, xml, mz, inten position tables
            div["dp"].append(pos)
            n = struct.unpack_from("<Q", data, pos)[0]
            pos += 8 + 16 * n
        pos += 8  # division size
        div["scans"] = pos
        pos += 4 + 4 * struct.unpack_from("<I", data, pos)[0]
        div["ms_levels"] = pos
        pos += 4 + 2 * struct.unpack_from("<I", data, pos)[0]
        divisions.append(div)
    assert pos == footer  # the index ends exactly where the footer starts
    return divisions


def _corrupt(src, dst, edit):
    shutil.copyfile(src, dst)
    data = bytearray(dst.read_bytes())
    edit(data, _division_layout(bytes(data)))
    dst.write_bytes(bytes(data))
    return dst


def _set_u64(data, offset, value):
    struct.pack_into("<Q", data, offset, value)


def _set_u32(data, offset, value):
    struct.pack_into("<I", data, offset, value)


def test_valid_file_still_opens(msz_file_path, shuffled_msz_file_path):
    for path in (msz_file_path, shuffled_msz_file_path):
        msz = read(path)
        assert len(msz.spectra) == 50


@pytest.mark.parametrize(
    "count",
    [0xFFFFFFFF, 2**61, 2**64 - 1],  # past the file / overflows 16*n / max
    ids=["huge", "overflows-byte-count", "u64-max"],
)
def test_position_table_count_past_footer(msz_file_path, tmp_path, count):
    bad = _corrupt(
        msz_file_path,
        tmp_path / "bad.msz",
        lambda d, divs: _set_u64(d, divs[0]["dp"][0], count),
    )
    with pytest.raises(MSCompressError, match="invalid division index") as exc:
        read(str(bad))
    assert "read_dp" in str(exc.value)


def test_scan_array_length_past_footer(msz_file_path, tmp_path):
    bad = _corrupt(
        msz_file_path,
        tmp_path / "bad.msz",
        lambda d, divs: _set_u32(d, divs[0]["scans"], 0xFFFFFFFF),
    )
    with pytest.raises(MSCompressError, match="read_uint32_arr"):
        read(str(bad))


def test_last_array_runs_two_bytes_past_footer(msz_file_path, tmp_path):
    def edit(d, divs):
        last = divs[-1]["ms_levels"]
        _set_u32(d, last, struct.unpack_from("<I", d, last)[0] + 1)

    bad = _corrupt(msz_file_path, tmp_path / "bad.msz", edit)
    with pytest.raises(MSCompressError, match="read_uint16_arr"):
        read(str(bad))


def test_inconsistent_spectrum_counts(msz_file_path, tmp_path):
    # Every length stays in bounds; only the counts disagree. Rewrite the first
    # division's scan array with n-1 entries and its MS-level array with n+2,
    # which keeps the section exactly the same size (4*(n-1) + 2*(n+2) = 6n).
    def edit(d, divs):
        scans, levels = divs[0]["scans"], divs[0]["ms_levels"]
        n = struct.unpack_from("<I", d, scans)[0]
        assert n >= 1
        end = levels + 4 + 2 * n
        new = (
            struct.pack("<I", n - 1)
            + bytes(d[scans + 4 : scans + 4 * n])  # first n-1 scan numbers
            + struct.pack("<I", n + 2)
            + bytes(d[levels + 4 : end])
            + b"\x00" * 4
        )
        assert len(new) == end - scans
        d[scans:end] = new

    bad = _corrupt(msz_file_path, tmp_path / "bad.msz", edit)
    with pytest.raises(MSCompressError, match="inconsistent spectrum counts"):
        read(str(bad))
