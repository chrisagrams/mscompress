import pytest
from xml.etree.ElementTree import Element

from mscompress import read


def test_get_header_basic(mzml_file_path):
    """Test that get_header returns a string containing valid XML."""
    with read(mzml_file_path) as f:
        header = f.get_header()
        assert isinstance(header, str)
        assert len(header) > 0
        # Check for XML declaration
        assert '<?xml' in header
        # Check for mzML tag
        assert '<mzML' in header


def test_get_header_contains_metadata_sections(mzml_file_path):
    """Test that the header contains expected metadata sections."""
    with read(mzml_file_path) as f:
        header = f.get_header()
        # Check for common mzML header sections
        assert 'cvList' in header
        assert 'fileDescription' in header
        assert 'referenceableParamGroupList' in header
        assert 'instrumentConfigurationList' in header


def test_get_header_stops_before_spectrum(mzml_file_path):
    """Test that header extraction stops before the first spectrum."""
    with read(mzml_file_path) as f:
        header = f.get_header()
        # The header should not contain the actual spectrum data
        # We expect to see the opening of spectrumList but not the full spectrum content
        assert '<spectrumList' in header


def test_extract_metadata_cvlist(mzml_file_path):
    """Test extracting the cvList metadata tag."""
    with read(mzml_file_path) as f:
        cv_list = f.extract_metadata('cvList')
        
        assert isinstance(cv_list, Element)
        assert cv_list.tag.endswith('cvList')
        
        # Check that we have cv elements
        cvs = list(cv_list)
        assert len(cvs) > 0
        
        # Check the first cv element has expected attributes
        cv = cvs[0]
        assert cv.tag.endswith('cv')
        assert 'id' in cv.attrib
        assert 'fullName' in cv.attrib


def test_extract_metadata_file_description(mzml_file_path):
    """Test extracting the fileDescription metadata tag."""
    with read(mzml_file_path) as f:
        file_desc = f.extract_metadata('fileDescription')
        
        assert isinstance(file_desc, Element)
        assert file_desc.tag.endswith('fileDescription')
        
        # Check for fileContent child
        file_content = file_desc.find('.//{http://psi.hupo.org/ms/mzml}fileContent')
        assert file_content is not None
        
        # Check for sourceFileList child
        source_file_list = file_desc.find('.//{http://psi.hupo.org/ms/mzml}sourceFileList')
        assert source_file_list is not None


def test_extract_metadata_referenceable_param_group_list(mzml_file_path):
    """Test extracting the referenceableParamGroupList metadata tag."""
    with read(mzml_file_path) as f:
        param_groups = f.extract_metadata('referenceableParamGroupList')
        
        assert isinstance(param_groups, Element)
        assert param_groups.tag.endswith('referenceableParamGroupList')
        
        # Check that we have referenceableParamGroup elements
        groups = list(param_groups)
        assert len(groups) > 0
        
        # Check the first group has an id attribute
        group = groups[0]
        assert 'id' in group.attrib


def test_extract_metadata_sample_list(mzml_file_path):
    """Test extracting the sampleList metadata tag."""
    with read(mzml_file_path) as f:
        sample_list = f.extract_metadata('sampleList')
        
        assert isinstance(sample_list, Element)
        assert sample_list.tag.endswith('sampleList')
        assert 'count' in sample_list.attrib
        
        # Check for sample elements
        samples = list(sample_list)
        assert len(samples) > 0


def test_extract_metadata_software_list(mzml_file_path):
    """Test extracting the softwareList metadata tag."""
    with read(mzml_file_path) as f:
        software_list = f.extract_metadata('softwareList')
        
        assert isinstance(software_list, Element)
        assert software_list.tag.endswith('softwareList')
        assert 'count' in software_list.attrib
        
        # Check for software elements
        software_items = list(software_list)
        assert len(software_items) > 0
        
        # Check that software elements have id and version
        for software in software_items:
            assert 'id' in software.attrib
            assert 'version' in software.attrib


def test_extract_metadata_instrument_configuration_list(mzml_file_path):
    """Test extracting the instrumentConfigurationList metadata tag."""
    with read(mzml_file_path) as f:
        instrument_list = f.extract_metadata('instrumentConfigurationList')
        
        assert isinstance(instrument_list, Element)
        assert instrument_list.tag.endswith('instrumentConfigurationList')
        assert 'count' in instrument_list.attrib
        
        # Check for instrumentConfiguration elements
        configs = list(instrument_list)
        assert len(configs) > 0


def test_extract_metadata_data_processing_list(mzml_file_path):
    """Test extracting the dataProcessingList metadata tag."""
    with read(mzml_file_path) as f:
        data_processing_list = f.extract_metadata('dataProcessingList')
        
        assert isinstance(data_processing_list, Element)
        assert data_processing_list.tag.endswith('dataProcessingList')
        assert 'count' in data_processing_list.attrib


def test_extract_metadata_nonexistent_tag(mzml_file_path):
    """Test that extracting a non-existent tag raises ValueError."""
    with read(mzml_file_path) as f:
        with pytest.raises(ValueError, match="Tag 'nonExistentTag' not found"):
            f.extract_metadata('nonExistentTag')


def test_extract_metadata_preserves_namespaces(mzml_file_path):
    """Test that namespace declarations are preserved in extracted metadata."""
    with read(mzml_file_path) as f:
        cv_list = f.extract_metadata('cvList')
        
        # The tag should include the namespace
        assert 'http://psi.hupo.org/ms/mzml' in cv_list.tag


def test_extract_metadata_multiple_calls(mzml_file_path):
    """Test that extract_metadata can be called multiple times for different tags."""
    with read(mzml_file_path) as f:
        cv_list = f.extract_metadata('cvList')
        file_desc = f.extract_metadata('fileDescription')
        software_list = f.extract_metadata('softwareList')
        
        assert isinstance(cv_list, Element)
        assert isinstance(file_desc, Element)
        assert isinstance(software_list, Element)
        
        # Verify they are different elements
        assert cv_list.tag != file_desc.tag
        assert file_desc.tag != software_list.tag


def test_extract_metadata_access_child_elements(mzml_file_path):
    """Test accessing and iterating over child elements of extracted metadata."""
    with read(mzml_file_path) as f:
        param_groups = f.extract_metadata('referenceableParamGroupList')
        
        # Iterate over child elements
        for group in param_groups:
            assert group.tag.endswith('referenceableParamGroup')
            assert 'id' in group.attrib
            
            # Access nested elements
            cv_params = group.findall('.//{http://psi.hupo.org/ms/mzml}cvParam')
            assert len(cv_params) > 0
            
            for param in cv_params:
                assert 'accession' in param.attrib
                assert 'name' in param.attrib


def test_extract_metadata_access_attributes(mzml_file_path):
    """Test accessing attributes of extracted metadata elements."""
    with read(mzml_file_path) as f:
        cv_list = f.extract_metadata('cvList')
        
        # Check count attribute
        assert 'count' in cv_list.attrib
        count = int(cv_list.attrib['count'])
        assert count > 0
        
        # Verify count matches number of cv elements
        cvs = list(cv_list)
        assert len(cvs) == count


def test_get_header_with_msz_file(msz_file_path):
    """Test that get_header works with MSZ (compressed) files."""
    with read(msz_file_path) as f:
        header = f.get_header()
        assert isinstance(header, str)
        assert len(header) > 0
        assert '<mzML' in header


def test_extract_metadata_with_msz_file(msz_file_path):
    """Test that extract_metadata works with MSZ (compressed) files."""
    with read(msz_file_path) as f:
        cv_list = f.extract_metadata('cvList')
        
        assert isinstance(cv_list, Element)
        assert cv_list.tag.endswith('cvList')
        
        cvs = list(cv_list)
        assert len(cvs) > 0


def test_extract_metadata_empty_tag_name(mzml_file_path):
    """Test that extract_metadata with empty tag name raises ValueError."""
    with read(mzml_file_path) as f:
        with pytest.raises(ValueError, match="not found"):
            f.extract_metadata('')


def test_get_header_consistency(mzml_file_path):
    """Test that get_header returns consistent results on multiple calls."""
    with read(mzml_file_path) as f:
        header1 = f.get_header()
        header2 = f.get_header()
        
        assert header1 == header2
        assert len(header1) == len(header2)


def test_extract_metadata_with_namespace_variations(mzml_file_path):
    """Test that extract_metadata works regardless of namespace prefixes."""
    with read(mzml_file_path) as f:
        # These should work even though mzML uses namespaces
        cv_list = f.extract_metadata('cvList')
        assert cv_list is not None
        
        sample_list = f.extract_metadata('sampleList')
        assert sample_list is not None
