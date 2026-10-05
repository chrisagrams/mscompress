# Versioning

The project version lives in **`.cz.toml`** at the repo root, managed by
[commitizen](https://commitizen-tools.github.io/commitizen/). Every
build artifact reads from it:

| Artifact | How it consumes `.cz.toml` |
|----------|---------------------------|
| C library / CLI | `CMakeLists.txt` parses with a regex into `PROJECT_VERSION` |
| C source | `cli/CMakeLists.txt` passes `VERSION` via `target_compile_definitions` |
| Python | `python/pyproject.toml` has a static `version` field, kept in sync by `cz bump` |
| npm | All `package.json` files updated by `cz bump` |

Never hardcode a version string anywhere. The build will fail if it
diverges.

## Bumping

```bash
uv tool run commitizen bump --increment PATCH    # 1.0.15 → 1.0.16
uv tool run commitizen bump --increment MINOR    # 1.0.15 → 1.1.0
uv tool run commitizen bump --increment MAJOR    # 1.0.15 → 2.0.0
```

`cz bump` updates every consumer listed in `.cz.toml`'s `version_files`
section, writes a changelog entry, and creates a git tag (currently
`create_tag = false`, so tag creation is manual).

## Release checklist

1. `cz bump --increment <kind>` from a clean main.
2. Inspect the diff — `.cz.toml`, all `package.json` files,
   `python/pyproject.toml`, `python/mscompress/__init__.py`.
3. Commit, tag, push, and push the tag.
4. CI builds, publishes wheels to PyPI, publishes the npm package
   (uploading prebuilds as Release assets), and tags a Docker image.
