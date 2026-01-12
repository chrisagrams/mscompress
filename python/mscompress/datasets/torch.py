"""PyTorch DataLoaders for MSCompress datasets."""
import mscompress
from mscompress.mszx import MSZXFile
from mscompress.annotations.psms import BasePSMReader
from mscompress.types import AnnotationFormat
from typing import Union, Optional, List, Dict, Any
from pathlib import Path
try:
    import torch
    from torch.utils.data import Dataset
except ImportError as e:
    raise ImportError(
        "PyTorch is required to use this module. Please install it via 'pip install torch'."
    ) from e


class MSCompressDatasetMember:
    def __init__(self, path: Union[str, Path], load_annotations: Optional[List[AnnotationFormat]] = None):
        self._path = Path(path)
        self._handle = mscompress.read(str(self._path))
        self._load_annotations: List[AnnotationFormat] = load_annotations or []
        self._annotation_readers: Dict[AnnotationFormat, List[BasePSMReader]] = {}
        
        # Load requested annotations if handle is MSZXFile
        if isinstance(self._handle, MSZXFile) and self._load_annotations:
            for annotation_format in self._load_annotations:
                readers = self._handle.get_annotation_readers_by_format(annotation_format)
                if readers:
                    self._annotation_readers[annotation_format] = readers
    
    @property
    def path(self):
        return self._path
    
    def __len__(self) -> int:
        """Return the number of spectra in the dataset member."""
        return len(self._handle.spectra)

    def __getitem__(
            self,
            index
        ) -> Union[
            tuple[torch.Tensor, torch.Tensor],
            tuple[torch.Tensor, torch.Tensor, Dict[str, Any]]
        ]:
        """Get spectrum by index in the MSZ/mzML file."""
        spectrum = self._handle.spectra[index]
        # Convert to tensors
        mz = torch.from_numpy(spectrum.mz)
        intensity = torch.from_numpy(spectrum.intensity)

        # If annotations are loaded, include them in the return value
        if self._annotation_readers:
            annotations_dict = {}
            scan_number = spectrum.scan
            
            for annotation_format, readers in self._annotation_readers.items():
                # Get PSMs for this scan number from all readers of this format
                format_psms = []
                for reader in readers:
                    psms = reader.get_by_scan(scan_number)
                    format_psms.extend(psms)
                annotations_dict[annotation_format] = format_psms
            
            return (mz, intensity, annotations_dict)
        
        return (mz, intensity)


class MSCompressDataset(Dataset):
    def __init__(
            self,
            path: Union[str, Path],
            load_annotations: Optional[List[AnnotationFormat]] = None
        ):
        """
        Initialize the MSCompress dataset.
        
        Args:
            path (Union[str, Path]): Path to a msz, mszx, or mzML file, or a directory containing such files.
            load_annotations (Optional[List[AnnotationFormat]]): List of annotation formats to load from MSZX files.
                Only applicable for MSZX files. Annotations will be returned in __getitem__ if present.
        """
        
        self._path = Path(path)
        self._load_annotations: List[AnnotationFormat] = load_annotations or []
        
        # Initialize dataset members
        self.members = {}
        if self._path.is_dir():
            for file in self._path.iterdir():
                if file.suffix.lower() in {'.msz', '.mszx', '.mzml'}:
                    member = MSCompressDatasetMember(file, load_annotations=self._load_annotations)
                    self.members[file.name] = member
        elif self._path.suffix.lower() in {'.msz', '.mszx', '.mzml'}:
            member = MSCompressDatasetMember(self._path, load_annotations=self._load_annotations)
            self.members[self._path.name] = member
        else:
            raise ValueError("Provided path is neither a valid file nor a directory containing valid files.")

        # Build index lookup
        self._index_lookup = {}
        global_index = 0
        for member in self.members.values():
            for local_index in range(len(member)):
                self._index_lookup[global_index] = (member, local_index)
                global_index += 1
        self._total_spectra = global_index

    @property
    def path(self):
        return self._path
    
    def __len__(self) -> int:
        """Return the total number of spectra in the dataset."""
        return self._total_spectra
    
    def __getitem__(self, index) -> Union[tuple[torch.Tensor, torch.Tensor], tuple[torch.Tensor, torch.Tensor, Dict[str, Any]]]:
        """Get spectrum by index across all dataset members."""
        if index < 0:
            index = self._total_spectra + index
        
        if index not in self._index_lookup:
            raise IndexError(f"Index {index} out of range for dataset with {self._total_spectra} spectra.")
        
        member, local_index = self._index_lookup[index]
        return member[local_index]
