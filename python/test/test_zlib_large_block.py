"""Regression test: get_xml() re-encoding of zlib arrays larger than 64 KB.

The vendored cloudflare zlib fork only slides its deflate hash tables when
built with HAS_SSE2 (x86-64) or on aarch64. Without it, any single deflate
input over ~64 KB (wsize + MAX_DIST = 65274 bytes) produced a corrupt stream
or segfaulted in bi_windup(). get_xml() on zlib-compressed source mzML
re-deflates every binary array, so spectra with >8159 float64 points crashed.
"""
import base64
import re
import zlib
import xml.etree.ElementTree as ET

import numpy as np
import pytest
from mscompress import read

_BDA = re.compile(r"(<binaryDataArray encodedLength=\")(\d+)(\">.*?<binary>)(.*?)(</binary>)", re.S)

# 8 bytes per float64; anything above this many points is a >64 KB deflate input
_DEFLATE_SLIDE_POINTS = 65274 // 8


_BIG_POINTS = 12000  # ~96 KB per float64 array


def _big_arrays():
    """Deterministic profile-like arrays: quantized values give deflate many matches."""
    rng = np.random.default_rng(0)
    mz = 200.0 + np.cumsum(np.round(rng.uniform(0.01, 0.3, _BIG_POINTS), 4))
    intensity = np.round(rng.gamma(1.5, 2e4, _BIG_POINTS), 1)
    return [mz, intensity]


def _zlib_mzml(src_path, dst_path):
    """Rewrite every binary array of src_path as zlib-compressed, with
    spectrum 0 enlarged to _BIG_POINTS so its arrays exceed 64 KB."""
    text = open(src_path, encoding="utf-8").read()

    first_end = text.index("</spectrum>")
    first_start = text.rindex("<spectrum ", 0, first_end)
    first = re.sub(r'defaultArrayLength="\d+"', f'defaultArrayLength="{_BIG_POINTS}"',
                   text[first_start:first_end], count=1)
    big = iter(_big_arrays())
    first = _BDA.sub(lambda m: m.group(0).replace(
        m.group(4), base64.b64encode(next(big).tobytes()).decode()), first)
    text = text[:first_start] + first + text[first_end:]

    def recompress(m):
        packed = base64.b64encode(zlib.compress(base64.b64decode(m.group(4)))).decode()
        head = m.group(3).replace('accession="MS:1000576" name="no compression"',
                                  'accession="MS:1000574" name="zlib compression"')
        return f"{m.group(1)}{len(packed)}{head}{packed}{m.group(5)}"

    text = _BDA.sub(recompress, text)
    # Byte offsets moved; drop the now-stale index rather than ship wrong offsets.
    text = re.sub(r"\s*<indexList.*?</indexList>", "", text, flags=re.S)
    text = re.sub(r"\s*<indexListOffset>.*?</indexListOffset>", "", text, flags=re.S)
    text = re.sub(r"\s*<fileChecksum>.*?</fileChecksum>", "", text, flags=re.S)
    open(dst_path, "w", encoding="utf-8").write(text)


def _decode_arrays(xml):
    out = []
    for block in re.findall(r"<binaryDataArray.*?</binaryDataArray>", xml, re.S):
        raw = base64.b64decode(re.search(r"<binary>(.*?)</binary>", block, re.S).group(1))
        if "MS:1000574" in block:
            raw = zlib.decompress(raw)
        out.append(np.frombuffer(raw, np.float64 if "MS:1000523" in block else np.float32))
    return out


@pytest.fixture
def zlib_msz(mzml_file_path, tmp_path):
    zlib_mzml = tmp_path / "zlib.mzML"
    _zlib_mzml(mzml_file_path, zlib_mzml)
    msz_path = tmp_path / "zlib.msz"
    with read(str(zlib_mzml)) as mzml:
        assert len(mzml.spectra[0].mz) == _BIG_POINTS > _DEFLATE_SLIDE_POINTS
        mzml.compress(msz_path)
    return msz_path


def test_get_xml_reencodes_large_zlib_arrays(zlib_msz):
    expected_mz, expected_intensity = _big_arrays()
    with read(str(zlib_msz)) as msz:
        xml = ET.tostring(msz.get_xml(0), encoding="unicode")
        mz, intensity = _decode_arrays(xml)
        np.testing.assert_array_equal(mz, expected_mz)
        np.testing.assert_array_equal(intensity, expected_intensity)
        np.testing.assert_array_equal(msz.spectra[0].mz, expected_mz)
        np.testing.assert_array_equal(msz.spectra[0].intensity, expected_intensity)


def test_decompress_reencodes_large_zlib_arrays(zlib_msz, tmp_path):
    out = tmp_path / "roundtrip.mzML"
    with read(str(zlib_msz)) as msz:
        msz.decompress(out)
    expected_mz, expected_intensity = _big_arrays()
    with read(str(out)) as mzml:
        np.testing.assert_array_equal(mzml.spectra[0].mz, expected_mz)
        np.testing.assert_array_equal(mzml.spectra[0].intensity, expected_intensity)
