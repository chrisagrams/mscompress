"""PyTorch DataLoaders for MSCompress datasets."""
import mscompress
from typing import Generator, Union
from pathlib import Path
try:
    import torch
    from torch.utils.data import Dataset, IterableDataset
except ImportError as e:
    raise ImportError(
        "PyTorch is required to use this module. Please install it via 'pip install torch'."
    ) from e


class MSCompressDatasetMember:
    def __init__(self, path: Union[str, Path]):
        self._path = Path(path)
        self._handle = mscompress.read(str(self._path))
    
    @property
    def path(self):
        return self._path
    
    def __len__(self) -> int:
        """Return the number of spectra in the dataset member."""
        return len(self._handle.spectra)

    def __getitem__(self, index) -> tuple[torch.Tensor, torch.Tensor]:
        """Get spectrum by index in the MSZ/mzML file."""
        spectrum = self._handle.spectra[index]
        # Convert to tensors
        mz = torch.from_numpy(spectrum.mz)
        intensity = torch.from_numpy(spectrum.intensity)

        sample = (mz, intensity)

        return sample


class MSCompressDataset(Dataset):
    def __init__(self, path: Union[str, Path]):
        """
        Initialize the MSCompress dataset.
        
        Args:
            path (Union[str, Path]): Path to a msz, mszx, or mzML file, or a directory containing such files.
        """
        
        self._path = Path(path)
        
        # Initialize dataset members
        self.members = {}
        if self._path.is_dir():
            for file in self._path.iterdir():
                if file.suffix.lower() in {'.msz', '.mszx', '.mzml'}:
                    member = MSCompressDatasetMember(file)
                    self.members[file.name] = member
        elif self._path.suffix.lower() in {'.msz', '.mszx', '.mzml'}:
            member = MSCompressDatasetMember(self._path)
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
    
    def __getitem__(self, index) -> tuple[torch.Tensor, torch.Tensor]:
        """Get spectrum by index across all dataset members."""
        if index < 0:
            index = self._total_spectra + index
        
        if index not in self._index_lookup:
            raise IndexError(f"Index {index} out of range for dataset with {self._total_spectra} spectra.")
        
        member, local_index = self._index_lookup[index]
        return member[local_index]
