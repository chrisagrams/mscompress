# vdelta (experimental)

Variable-precision delta encoding — like `delta16`/`delta24`, but the bit
width is chosen per delta instead of being fixed for the whole stream.

!!! warning "Experimental"
    Marked `experimental = 1` in `algo_registry`. Behavior, parameters,
    and on-disk encoding may change without notice. Not recommended for
    long-term archival.

| Variant | Source |
|---------|--------|
| `vdelta16` | `src/algos/vdelta.c` |
| `vdelta24` | `src/algos/vdelta.c` |

- **Target stream:** m/z
- **Loss:** Yes — depends on per-block width selection
