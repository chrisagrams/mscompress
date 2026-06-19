# Implementing a new algorithm

End-to-end recipe for adding a new pre-compression transform. The example
threads through naming a hypothetical `myalgo` algorithm that handles 32-bit
and 64-bit source data on the m/z stream.

## 1. Pick an ID

Algorithm IDs are small integer constants. The current range is
`4700000..4700012` (see `src/mscompress.h`). Add the next unused value:

```c
// src/mscompress.h
#define _myalgo_ 4700020
```

## 2. Implement the encoder and decoder

Create `src/algos/myalgo.c` with one pair of functions per source data type
you want to support. Use the existing algorithms as reference; the simplest
template is `src/algos/lossless.c` (pass-through), and `src/algos/delta.c`
shows the standard pattern of reading typed input and writing quantized
output.

```c
// src/algos/myalgo.c
#include "../mscompress.h"
#include "algos.h"

void algo_encode_myalgo_64d(void* args) {
   algo_args* a = (algo_args*)args;
   const double* src = (const double*)a->src;
   uint8_t* dest = (uint8_t*)a->dest;
   size_t n = a->src_len / sizeof(double);

   // ... your transform ...

   *a->dest_len = bytes_written;
}

void algo_decode_myalgo_64d(void* args) {
   algo_args* a = (algo_args*)args;
   const uint8_t* src = (const uint8_t*)a->src;
   double* dest = (double*)a->dest;

   // ... inverse transform ...

   *a->dest_len = bytes_written;
}

// Repeat for _32f if the algorithm supports float input.
```

### Contract

- `args->src` and `args->dest` are caller-owned buffers. Don't store the
  pointers and don't free either one.
- Read exactly `args->src_len` bytes; write the encoded result to `dest` and
  set `*args->dest_len` to the number of bytes written.
- Decoders must reverse exactly the byte sequence the encoder produced for
  the same `algo_args.mz_scale_factor` / `int_scale_factor`. Round-trip
  correctness is enforced by the test suite.

## 3. Declare the functions

```c
// src/algos/algos.h
void algo_encode_myalgo_64d(void* args);
void algo_decode_myalgo_64d(void* args);
void algo_encode_myalgo_32f(void* args);
void algo_decode_myalgo_32f(void* args);
```

## 4. Register it

```c
// src/algo.c, in algo_registry[]
{"myalgo", _myalgo_, TARGET_MZ, "My algorithm description", 1.0f, 0, 0},
```

`target` is a bitmask of `TARGET_MZ` and/or `TARGET_INT`. `experimental=1`
makes it visible but hides it from default help text.

## 5. Wire up dispatch

Add `case` clauses to both switches in `src/algo.c`:

```c
// In set_compress_algo()
case _myalgo_: {
   switch (accession) {
      case _32f_: return algo_encode_myalgo_32f;
      case _64d_: return algo_encode_myalgo_64d;
   }
   break;
}

// In set_decompress_algo()
case _myalgo_: {
   switch (accession) {
      case _32f_: return algo_decode_myalgo_32f;
      case _64d_: return algo_decode_myalgo_64d;
   }
   break;
}
```

## 6. Add to the build

The source list lives in `cli/CMakeLists.txt` for the CLI and in `setup.py`
for the Python bindings. Both compile every `.c` file under `src/algos/` —
adding a new file there is enough, but verify after editing.

## 7. Test it

Add a C test and a Python test that round-trip a known array:

```c
// cli/test/test_myalgo.c
#include "../../src/mscompress.h"
// ... harness ...

void test_myalgo_roundtrip() {
   double in[] = { /* known values */ };
   // encode → decode → assert byte-exact (lossless) or assert within tolerance (lossy)
}
```

```python
# python/test/test_myalgo.py
def test_myalgo_roundtrip(tmp_path, mzml_fixture):
    out = tmp_path / "out.msz"
    with mscompress.read(mzml_fixture) as f:
        args = mscompress.RuntimeArguments()
        args.mz_lossy = "myalgo"
        f.compress(out, arguments=args)
    with mscompress.read(out) as f:
        # ... assert spectra survive round-trip ...
```

Also run `mscompress --list-algorithms` to confirm the new entry shows up.

## 8. Document it

Add a page under `docs/algorithms/catalog/` describing:

- The math behind the transform
- Target stream (m/z, intensity, or both)
- The default scale factor and how to tune it
- Lossless vs lossy character and expected precision
- When this algorithm is the right choice

Then link it from `docs/algorithms/catalog/index.md` and the mkdocs nav.

## Worked example

`src/algos/delta.c` is the cleanest worked example — it ships three variants
(`delta16`, `delta24`, `delta32`) of the same pattern (compute deltas,
quantize, pack), registered as three separate entries with three different
scale factors. Read it alongside `set_compress_algo()` / `set_decompress_algo()`
to see the full wiring.
