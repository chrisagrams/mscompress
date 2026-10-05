# Filtered extraction

Pull a subset of spectra out of an mzML or MSZ without re-reading the
whole file.

```ts
import { read, type ExtractOptions } from "mscompress";

const f = read("data.msz");

// By index
f.extract("subset.mzML", { indices: [0, 1, 100, 5000] });

// By scan number
f.extract("subset.mzML", { scanNumbers: [42, 100, 1337] });

// By MS level
f.extract("ms2.mzML", { msLevel: 2 });

// Combined (AND)
f.extract("ms2_subset.mzML", { msLevel: 2, indices: [0, 1, 2] });

f.close();
```

## Why this is fast on MSZ

MSZ's footer maps each spectrum index to its byte ranges in the three
streams. The extractor decompresses only the blocks that contain selected
spectra, with caching to keep work down when multiple selected spectra
share a block. Cost is O(spectra selected), not O(file size).
