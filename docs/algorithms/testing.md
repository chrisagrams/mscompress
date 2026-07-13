# Testing algorithms

A new algorithm needs three tests:

1. **C unit test** — direct encode/decode round trip on a known buffer.
2. **C integration test** — compress and decompress a small mzML fixture
   through `mscompress` end to end.
3. **Python/TS round-trip test** — open an mzML, compress with the new
   `--mz-lossy` / `--int-lossy` setting, read back, compare.

## C tests

```bash
ctest --test-dir cli
ctest --test-dir cli -R MyAlgo        # filter by name
```

C tests live in `cli/test/`. Each test is a small executable registered with
`add_test()` in `cli/CMakeLists.txt`.

## Python tests

```bash
cd python && uv run pytest
cd python && uv run pytest test/test_transforms.py::test_my_algo -v
```

## Tolerance for lossy algorithms

A lossless algorithm round-trips bit-exact. Lossy algorithms need an explicit
tolerance:

```python
import numpy as np

# For delta32 on m/z arrays, error should be under ~4e-6 Da
np.testing.assert_allclose(roundtripped_mz, original_mz, atol=4e-6, rtol=0)
```

Document the expected tolerance on the algorithm's catalog page so users
know what they're trading away.

## Cross-language consistency

The same MSZ file produced by the CLI must read identically in Python and
TypeScript. The CI matrix builds all three and runs a round-trip test across
them; if you add an algorithm, make sure the CI fixture set covers it.
