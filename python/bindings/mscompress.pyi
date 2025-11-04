"""A versatile compression tool for efficient management of mass-spectrometry data."""

from typing import Union, Iterator, Optional, Dict, Any
from xml.etree.ElementTree import Element
import numpy as np
import numpy.typing as npt

class RuntimeArguments:
    """Runtime configuration arguments for compression/decompression operations."""
    
    threads: int
    blocksize: int
    mz_scale_factor: int
    int_scale_factor: int
    target_xml_format: int
    target_mz_format: int
    target_inten_format: int
    zstd_compression_level: int
    
    def __init__(self) -> None: ...

class DataFormat:
    """Data format information for mzML/MSZ files."""
    
    @property
    def source_mz_fmt(self) -> int: ...
    
    @property
    def source_inten_fmt(self) -> int: ...
    
    @property
    def source_compression(self) -> int: ...
    
    @property
    def source_total_spec(self) -> int: ...
    
    @property
    def target_xml_format(self) -> int: ...
    
    @property
    def target_mz_format(self) -> int: ...
    
    @property
    def target_inten_format(self) -> int: ...
    
    def __str__(self) -> str: ...
    
    def to_dict(self) -> Dict[str, Union[str, int]]: ...

class DataPositions:
    """Position information for data blocks in files."""
    
    @property
    def start_positions(self) -> npt.NDArray[np.uint64]: ...
    
    @property
    def end_positions(self) -> npt.NDArray[np.uint64]: ...
    
    @property
    def total_spec(self) -> int: ...

class Division:
    """Division structure containing data positions and scan information."""
    
    @property
    def spectra(self) -> DataPositions: ...
    
    @property
    def xml(self) -> DataPositions: ...
    
    @property
    def mz(self) -> DataPositions: ...
    
    @property
    def inten(self) -> DataPositions: ...
    
    @property
    def size(self) -> int: ...
    
    @property
    def scans(self) -> npt.NDArray[np.uint32]: ...
    
    @property
    def ms_levels(self) -> npt.NDArray[np.uint16]: ...
    
    @property
    def ret_times(self) -> Optional[npt.NDArray[np.float32]]: ...

class BaseFile:
    """Base class for mzML and MSZ file handlers."""
    
    @property
    def path(self) -> bytes: ...
    
    @property
    def filesize(self) -> int: ...
    
    @property
    def format(self) -> DataFormat: ...
    
    @property
    def spectra(self) -> Spectra: ...
    
    @property
    def positions(self) -> Division: ...
    
    @property
    def arguments(self) -> RuntimeArguments: ...
    
    def __enter__(self) -> BaseFile: ...
    
    def __exit__(
        self,
        exc_type: Optional[type],
        exc_value: Optional[BaseException],
        traceback: Optional[Any]
    ) -> None: ...
    
    def get_mz_binary(self, index: int) -> npt.NDArray[Union[np.float32, np.float64]]: ...
    
    def get_inten_binary(self, index: int) -> npt.NDArray[Union[np.float32, np.float64]]: ...
    
    def get_xml(self, index: int) -> Element: ...
    
    def describe(self) -> Dict[str, Any]: ...
    
    def compress(self, output: Union[str, bytes]) -> None: ...
    
    def decompress(self, output: Union[str, bytes]) -> None: ...

class MZMLFile(BaseFile):
    """Handler for mzML format files."""
    
    def __init__(self, path: bytes, filesize: int, fd: int) -> None: ...
    
    def compress(self, output: Union[str, bytes]) -> None:
        """
        Compress an mzML file to MSZ format.
        
        Parameters:
            output: Output file path (string or bytes).
        """
        ...
    
    def get_mz_binary(self, index: int) -> npt.NDArray[Union[np.float32, np.float64]]:
        """
        Extract m/z binary array for a spectrum at the given index.
        
        Parameters:
            index: Spectrum index.
            
        Returns:
            NumPy array of m/z values.
        """
        ...
    
    def get_inten_binary(self, index: int) -> npt.NDArray[Union[np.float32, np.float64]]:
        """
        Extract intensity binary array for a spectrum at the given index.
        
        Parameters:
            index: Spectrum index.
            
        Returns:
            NumPy array of intensity values.
        """
        ...
    
    def get_xml(self, index: int) -> Element:
        """
        Extract XML element for a spectrum at the given index.
        
        Parameters:
            index: Spectrum index.
            
        Returns:
            ElementTree Element containing spectrum metadata.
        """
        ...

class MSZFile(BaseFile):
    """Handler for MSZ (compressed) format files."""
    
    def __init__(self, path: bytes, filesize: int, fd: int) -> None: ...
    
    def decompress(self, output: Union[str, bytes]) -> None:
        """
        Decompress an MSZ file to mzML format.
        
        Parameters:
            output: Output file path (string or bytes).
        """
        ...
    
    def get_mz_binary(self, index: int) -> npt.NDArray[Union[np.float32, np.float64]]:
        """
        Extract m/z binary array for a spectrum at the given index.
        
        Parameters:
            index: Spectrum index.
            
        Returns:
            NumPy array of m/z values.
        """
        ...
    
    def get_inten_binary(self, index: int) -> npt.NDArray[Union[np.float32, np.float64]]:
        """
        Extract intensity binary array for a spectrum at the given index.
        
        Parameters:
            index: Spectrum index.
            
        Returns:
            NumPy array of intensity values.
        """
        ...
    
    def get_xml(self, index: int) -> Element:
        """
        Extract XML element for a spectrum at the given index.
        
        Parameters:
            index: Spectrum index.
            
        Returns:
            ElementTree Element containing spectrum metadata.
        """
        ...

class Spectrum:
    """Represents a single mass spectrum."""
    
    index: int
    scan: int
    ms_level: int
    
    def __init__(
        self,
        index: int,
        scan: int,
        ms_level: int,
        retention_time: float,
        file: BaseFile
    ) -> None: ...
    
    def __repr__(self) -> str: ...
    
    @property
    def xml(self) -> Element:
        """XML metadata for this spectrum."""
        ...
    
    @property
    def size(self) -> int:
        """Number of m/z - intensity pairs."""
        ...
    
    @property
    def retention_time(self) -> float:
        """Retention time of this spectrum."""
        ...
    
    @property
    def mz(self) -> npt.NDArray[Union[np.float32, np.float64]]:
        """m/z values array."""
        ...
    
    @property
    def intensity(self) -> npt.NDArray[Union[np.float32, np.float64]]:
        """Intensity values array."""
        ...
    
    @property
    def peaks(self) -> npt.NDArray[Union[np.float32, np.float64]]:
        """Combined m/z and intensity as 2D array."""
        ...

class Spectra:
    """
    Collection of spectra with lazy loading and iteration support.
    
    Allows indexing and iteration over all spectra in a file.
    """
    
    def __init__(
        self,
        f: BaseFile,
        df: DataFormat,
        positions: Division
    ) -> None: ...
    
    def __iter__(self) -> Iterator[Spectrum]: ...
    
    def __next__(self) -> Spectrum: ...
    
    def __getitem__(self, index: int) -> Spectrum: ...
    
    def __len__(self) -> int: ...

def read(path: Union[str, bytes]) -> Union[MZMLFile, MSZFile]:
    """
    Opens and parses mzML or MSZ file.
    
    Parameters:
        path: Path to file (string or bytes).
        
    Returns:
        MZMLFile or MSZFile object depending on file type.
        
    Raises:
        FileNotFoundError: If file does not exist.
        IsADirectoryError: If path points to a directory.
        OSError: If file type cannot be determined.
    """
    ...

def get_num_threads() -> int:
    """
    Get the number of available threads on the system.
    
    Returns:
        Number of usable threads.
    """
    ...

def get_filesize(path: Union[str, bytes]) -> int:
    """
    Get the size of a file in bytes.
    
    Parameters:
        path: Path to file (string or bytes).
        
    Returns:
        Size of the file in bytes.
        
    Raises:
        FileNotFoundError: If file does not exist.
    """
    ...
