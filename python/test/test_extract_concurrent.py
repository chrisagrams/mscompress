import tempfile
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed

from mscompress import read, MSZFile, MZMLFile


def _extract_msz_to_msz(msz_file_path, output_path, scan_numbers):
    """Worker function: extract specific scans from MSZ to MSZ."""
    with read(msz_file_path) as msz:
        msz.extract(output=output_path, scan_numbers=scan_numbers)


def _extract_msz_to_mzml(msz_file_path, output_path, scan_numbers):
    """Worker function: extract specific scans from MSZ to mzML."""
    with read(msz_file_path) as msz:
        msz.extract(output=output_path, scan_numbers=scan_numbers)


def _extract_mzml_to_msz(mzml_file_path, output_path, scan_numbers):
    """Worker function: extract specific scans from mzML to MSZ."""
    with read(mzml_file_path) as mzml:
        mzml.extract(output=output_path, scan_numbers=scan_numbers)


def test_concurrent_msz_to_msz_extract(msz_file_path):
    """Concurrent MSZ->MSZ extractions must not collide on temp paths."""
    with tempfile.TemporaryDirectory() as tmp:
        tasks = [
            (msz_file_path, str(Path(tmp) / "out_a.msz"), [1, 2]),
            (msz_file_path, str(Path(tmp) / "out_b.msz"), [3, 4]),
            (msz_file_path, str(Path(tmp) / "out_c.msz"), [1, 5]),
        ]

        with ThreadPoolExecutor(max_workers=3) as pool:
            futures = {
                pool.submit(_extract_msz_to_msz, *t): t for t in tasks
            }
            for f in as_completed(futures):
                f.result()  # propagate any exception

        # Verify each output independently
        with read(str(Path(tmp) / "out_a.msz")) as a:
            assert len(a.spectra) == 2
            assert [s.scan for s in a.spectra] == [1, 2]

        with read(str(Path(tmp) / "out_b.msz")) as b:
            assert len(b.spectra) == 2
            assert [s.scan for s in b.spectra] == [3, 4]

        with read(str(Path(tmp) / "out_c.msz")) as c:
            assert len(c.spectra) == 2
            assert [s.scan for s in c.spectra] == [1, 5]


def test_concurrent_msz_to_mzml_extract(msz_file_path):
    """Concurrent MSZ->mzML extractions must not collide on temp paths."""
    with tempfile.TemporaryDirectory() as tmp:
        tasks = [
            (msz_file_path, str(Path(tmp) / "out_a.mzML"), [1, 2]),
            (msz_file_path, str(Path(tmp) / "out_b.mzML"), [3, 4]),
            (msz_file_path, str(Path(tmp) / "out_c.mzML"), [1, 5]),
        ]

        with ThreadPoolExecutor(max_workers=3) as pool:
            futures = {
                pool.submit(_extract_msz_to_mzml, *t): t for t in tasks
            }
            for f in as_completed(futures):
                f.result()

        with read(str(Path(tmp) / "out_a.mzML")) as a:
            assert len(a.spectra) == 2
            assert [s.scan for s in a.spectra] == [1, 2]

        with read(str(Path(tmp) / "out_b.mzML")) as b:
            assert len(b.spectra) == 2
            assert [s.scan for s in b.spectra] == [3, 4]

        with read(str(Path(tmp) / "out_c.mzML")) as c:
            assert len(c.spectra) == 2
            assert [s.scan for s in c.spectra] == [1, 5]


def test_concurrent_mzml_to_msz_extract(mzml_file_path):
    """Concurrent mzML->MSZ extractions must not collide on temp paths."""
    with tempfile.TemporaryDirectory() as tmp:
        tasks = [
            (mzml_file_path, str(Path(tmp) / "out_a.msz"), [1, 2]),
            (mzml_file_path, str(Path(tmp) / "out_b.msz"), [3, 4]),
            (mzml_file_path, str(Path(tmp) / "out_c.msz"), [1, 5]),
        ]

        with ThreadPoolExecutor(max_workers=3) as pool:
            futures = {
                pool.submit(_extract_mzml_to_msz, *t): t for t in tasks
            }
            for f in as_completed(futures):
                f.result()

        with read(str(Path(tmp) / "out_a.msz")) as a:
            assert len(a.spectra) == 2
            assert [s.scan for s in a.spectra] == [1, 2]

        with read(str(Path(tmp) / "out_b.msz")) as b:
            assert len(b.spectra) == 2
            assert [s.scan for s in b.spectra] == [3, 4]

        with read(str(Path(tmp) / "out_c.msz")) as c:
            assert len(c.spectra) == 2
            assert [s.scan for s in c.spectra] == [1, 5]
