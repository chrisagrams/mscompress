from mscompress.annotations.psms import PepXMLReader
from mscompress.annotations.psms import TSVReader

def test_pepxml_reader(pepxml_file_path):
    reader = PepXMLReader(pepxml_file_path)
    psms = list(reader)
    assert len(psms) == 100
    assert psms[0].scan_number == 493
    assert psms[0].peptide == 'KSHHANSPTAGAAK'

def test_percolator_tsv_reader(percolator_tsv_file_path):
    reader = TSVReader(percolator_tsv_file_path)
    psms = list(reader)
    assert len(psms) == 100
    assert psms[0].scan_number == 4310
    assert psms[0].peptide == 'TDLNPDNLQGGDDLDPNYVLSSR'
