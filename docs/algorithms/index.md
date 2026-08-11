# Algorithms

A **pre-compression transform** that runs on the m/z or intensity array before
ZSTD touches it. The right transform can take an already-good ZSTD result and
shrink it by 2–5x more.

- **[How it works](how-it-works.md)** — the dispatch system, the registry,
  the encoder/decoder contract.
- **[Catalog](catalog/index.md)** — one page per registered algorithm with
  math, precision, and when to use it.
- **[Implementing a new algorithm](implementing.md)** — step-by-step
  contributor guide.
- **[Testing algorithms](testing.md)** — round-trip correctness, lossy
  tolerance harnesses.
