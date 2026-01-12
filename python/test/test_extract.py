from pathlib import Path
from mscompress import read
from mscompress import MSZFile

def test_extract_msz_to_mzml_index(msz_file_path):
    with read(msz_file_path) as msz:
        assert isinstance(msz, MSZFile)
        output_path = Path(msz_file_path).with_suffix('.extracted.mzML')
        msz.extract(output=output_path, indicies=[0, 2, 4])
        try:
            with read(output_path) as extracted:
                assert len(extracted.spectra) == 3
                assert extracted.spectra[0].index == 0
                assert extracted.spectra[0].scan == 1
                assert extracted.spectra[1].index == 1
                assert extracted.spectra[1].scan == 3
                assert extracted.spectra[2].index == 2
                assert extracted.spectra[2].scan == 5
        finally:
            output_path.unlink(missing_ok=True)

def test_extract_msz_to_mzml_scan(msz_file_path):
    with read(msz_file_path) as msz:
        assert isinstance(msz, MSZFile)
        output_path = Path(msz_file_path).with_suffix('.extracted.mzML')
        msz.extract(output=output_path, scan_numbers=[2, 4])
        
        try:
            with read(output_path) as extracted:
                assert len(extracted.spectra) == 2
                assert extracted.spectra[0].index == 0
                assert extracted.spectra[0].scan == 2
                assert extracted.spectra[1].index == 1
                assert extracted.spectra[1].scan == 4
        finally:
            output_path.unlink(missing_ok=True)

def test_extract_msz_to_mzml_ms_level(msz_file_path):
    with read(msz_file_path) as msz:
        assert isinstance(msz, MSZFile)
        output_path = Path(msz_file_path).with_suffix('.extracted.mzML')
        msz.extract(output=output_path, ms_level=2)
        
        try:
            with read(output_path) as extracted:
                for spectrum in extracted.spectra:
                    assert spectrum.ms_level == 2
        finally:
            output_path.unlink(missing_ok=True)