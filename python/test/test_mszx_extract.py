import pytest
from pathlib import Path
from mscompress.mszx import MSZXFile, create_mszx
from mscompress import read

# Test data paths
DATA_DIR = Path(__file__).parent / "data"
TEST_MSZ = DATA_DIR / "test.msz" 

@pytest.fixture
def msz_file():
    if not TEST_MSZ.exists():
        pytest.skip("test.msz not found")
    return read(str(TEST_MSZ))

@pytest.fixture
def dummy_pin(tmp_path):
    # distinct scan numbers: 100, 101, 102
    content = """SpecId\tLabel\tScanNr\tPeptide\tProteins\tScore
run.100.100.2\t1\t100\tPEPTIDEA\tPROT1\t0.9
run.101.101.2\t1\t101\tPEPTIDEB\tPROT2\t0.8
run.102.102.2\t-1\t102\tPEPTIDEC\tDECOY1\t0.1
"""
    p = tmp_path / "test.pin"
    p.write_text(content)
    return p

@pytest.fixture
def dummy_pepxml(tmp_path):
    # distinct scan numbers: 100, 101, 102
    content = """<?xml version="1.0" encoding="UTF-8"?>
<msms_pipeline_analysis>
  <msms_run_summary>
    <spectrum_query spectrum="run.100.100.2" start_scan="100" end_scan="100" precursor_neutral_mass="1000.0" assumed_charge="2">
      <search_result>
        <search_hit hit_rank="1" peptide="PEPTIDEA" protein="PROT1"/>
      </search_result>
    </spectrum_query>
    <spectrum_query spectrum="run.101.101.2" start_scan="101" end_scan="101" precursor_neutral_mass="1100.0" assumed_charge="2">
      <search_result>
        <search_hit hit_rank="1" peptide="PEPTIDEB" protein="PROT2"/>
      </search_result>
    </spectrum_query>
    <spectrum_query spectrum="run.102.102.2" start_scan="102" end_scan="102" precursor_neutral_mass="1200.0" assumed_charge="2">
      <search_result>
        <search_hit hit_rank="1" peptide="PEPTIDEC" protein="DECOY1"/>
      </search_result>
    </spectrum_query>
  </msms_run_summary>
</msms_pipeline_analysis>
"""
    p = tmp_path / "test.pep.xml"
    p.write_text(content)
    return p

def test_mszx_extract_filter_scans(tmp_path, msz_file, dummy_pin, dummy_pepxml):
    # 1. Create source MSZX
    mszx_path = tmp_path / "source.mszx"
    create_mszx(
        msz_file,
        mszx_path,
        annotations=[dummy_pin, dummy_pepxml],
        description="Source Archive"
    )
    
    assert mszx_path.exists()
    
    # 2. Extract subset of scans (e.g. 100 and 102)
    scans = msz_file.positions.scans
    scan1 = int(scans[0])
    scan2 = int(scans[1])
    # scan3 = int(scans[2])
    
    # Update dummy pin
    content = f"""SpecId\tLabel\tScanNr\tPeptide\tProteins\tScore
run.{scan1}.{scan1}.2\t1\t{scan1}\tPEPTIDEA\tPROT1\t0.9
run.{scan2}.{scan2}.2\t1\t{scan2}\tPEPTIDEB\tPROT2\t0.8
run.999.999.2\t-1\t999\tPEPTIDEC\tDECOY1\t0.1
"""
    dummy_pin.write_text(content)
    
    # Update dummy pepxml
    content = f"""<?xml version="1.0" encoding="UTF-8"?>
<msms_pipeline_analysis>
  <msms_run_summary>
    <spectrum_query spectrum="run.{scan1}.{scan1}.2" start_scan="{scan1}" end_scan="{scan1}" precursor_neutral_mass="1000.0" assumed_charge="2">
      <search_result>
        <search_hit hit_rank="1" peptide="PEPTIDEA" protein="PROT1"/>
      </search_result>
    </spectrum_query>
    <spectrum_query spectrum="run.{scan2}.{scan2}.2" start_scan="{scan2}" end_scan="{scan2}" precursor_neutral_mass="1100.0" assumed_charge="2">
      <search_result>
        <search_hit hit_rank="1" peptide="PEPTIDEB" protein="PROT2"/>
      </search_result>
    </spectrum_query>
    <spectrum_query spectrum="run.999.999.2" start_scan="999" end_scan="999" precursor_neutral_mass="1200.0" assumed_charge="2">
      <search_result>
        <search_hit hit_rank="1" peptide="PEPTIDEC" protein="DECOY1"/>
      </search_result>
    </spectrum_query>
  </msms_run_summary>
</msms_pipeline_analysis>
"""
    dummy_pepxml.write_text(content)
    
    # Re-create source MSZX with corrected annotations
    if mszx_path.exists():
        mszx_path.unlink()
        
    create_mszx(
        msz_file,
        mszx_path,
        annotations=[dummy_pin, dummy_pepxml],
        description="Source Archive"
    )

    # 3. Extract only scan1
    with MSZXFile.open(mszx_path) as source:
        output_path = tmp_path / "extracted.mszx"
        
        # Extract using specific scan number
        extracted = source.extract(output_path, scan_numbers=[scan1])
        
        try:
            # Verify MSZ content
            assert len(extracted.spectra) == 1
            assert extracted.spectra[0].scan == scan1
            
            # Verify Annotations
            pin_reader = extracted.get_annotation_reader("test.pin.zst")
            assert len(pin_reader) == 1
            assert pin_reader[0].scan_number == scan1
            
            # Check PepXML
            pep_reader = extracted.get_annotation_reader("test.pep.xml.zst")
            assert len(pep_reader) == 1
            assert pep_reader[0].scan_number == scan1
            
        finally:
            extracted.close()

def test_mszx_extract_filter_ms_level(tmp_path, msz_file, dummy_pin):
    # Check levels first
    levels = msz_file.positions.ms_levels
    unique_levels = set(levels)
    
    if len(unique_levels) < 2:
        pytest.skip("Test MSZ file does not have multiple MS levels")

    target_level = list(unique_levels)[0]
    
    # Setup annotation with one fake hit for a scan of that level
    scans = msz_file.positions.scans
    target_scan = None
    for i, lvl in enumerate(levels):
        if lvl == target_level:
            target_scan = int(scans[i])
            break
            
    content = f"""SpecId\tLabel\tScanNr\tPeptide\tProteins\tScore
run.{target_scan}.{target_scan}.2\t1\t{target_scan}\tPEPTIDEA\tPROT1\t0.9
"""
    dummy_pin.write_text(content)
    
    mszx_path = tmp_path / "level_test.mszx"
    create_mszx(msz_file, mszx_path, annotations=[dummy_pin])
    
    with MSZXFile.open(mszx_path) as source:
        output_path = tmp_path / "extracted_level.mszx"
        extracted = source.extract(output_path, ms_level=target_level)
        
        try:
            # Verify all extracted spectra have correct level
            extracted_levels = extracted.positions.ms_levels
            assert all(l == target_level for l in extracted_levels)
            
            # Verify annotation is preserved (since it matches a scan in the level)
            pin_reader = extracted.get_annotation_reader("test.pin.zst")
            assert len(pin_reader) == 1
            assert pin_reader[0].scan_number == target_scan
            
        finally:
            extracted.close()
