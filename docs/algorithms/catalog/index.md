# Algorithm catalog

Every registered transform, what it does, and when to use it. For the dispatch
machinery, see [How it works](../how-it-works.md).

| Algorithm | Target | Loss | Source file |
|-----------|--------|------|-------------|
| [lossless](lossless.md) | both | lossless | `src/algos/lossless.c` |
| [cast / cast16](cast.md) | m/z | lossy | `src/algos/cast.c` |
| [delta (16/24/32)](delta.md) | m/z | lossy | `src/algos/delta.c` |
| [bitpack](bitpack.md) | m/z | lossy | `src/algos/bitpack.c` |
| [log](log.md) | intensity | lossy | `src/algos/log2.c` |
| [vbr](vbr.md) | both | lossy | `src/algos/vbr.c` |
| [vdelta (experimental)](vdelta.md) | m/z | lossy | `src/algos/vdelta.c` |
