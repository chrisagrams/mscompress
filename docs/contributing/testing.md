# Running tests

Test data is shared across all three test suites — `test/data/` at the
repo root.

## C / CLI (ctest)

```bash
cmake -S . -B .
cmake --build .
ctest --test-dir cli
ctest --test-dir cli -R CompressTest                # filter by name
ctest --test-dir cli --output-on-failure            # verbose on failure
```

## Python (pytest)

```bash
cd python
uv run pytest
uv run pytest test/test_mzml.py                     # single file
uv run pytest test/test_mzml.py::test_mzml_open -v  # single test
```

## Node.js (vitest)

```bash
cd node-ts
npm test
npm run test:watch
```

## Cross-language round trip

The CI matrix builds all three and runs a small round-trip test (write
in one, read in another) on every PR. If you add an algorithm or change
the format, make sure the cross-language fixture set covers it.

## Test data conventions

- `test/data/*.mzML` — input fixtures
- `test/data/*.msz` — pre-built MSZ fixtures
- `test/data/*.mszx` — pre-built MSZX fixtures
- Per-test temp directories go to `tmp_path` (pytest) or the equivalent
  in each suite — never write into `test/data/`.
