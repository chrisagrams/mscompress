# Installation

=== "Python"

    ```bash
    pip install mscompress
    ```

    Optional extras:

    ```bash
    pip install 'mscompress[parquet]'   # Parquet round-tripping
    pip install 'mscompress[jax]'       # JAX/grain dataset adapter
    pip install 'mscompress[torch]'     # PyTorch Dataset adapter
    ```

    With [uv](https://docs.astral.sh/uv/):

    ```bash
    uv add mscompress
    uv add 'mscompress[parquet,jax]'
    ```

    Wheels are published for CPython 3.9–3.13 on macOS (x86_64, arm64),
    Linux (x86_64, aarch64), and Windows (x64).

=== "Node.js"

    ```bash
    npm install mscompress
    ```

    Prebuilt binaries are downloaded automatically for:

    - macOS (x64, arm64)
    - Linux (x64, arm64)
    - Windows (x64)

    On unsupported platforms or if the prebuild download fails, `cmake-js`
    falls back to building from source. Source builds require Python 3, a
    C++17 toolchain, and CMake 3.15+.

=== "CLI"

    See [Build from source](../cli/build-from-source.md) for the full path.
    Quick build from the repository root:

    ```bash
    cmake -S . -B .
    cmake --build .
    ./cli/mscompress --version
    ```

=== "Docker"

    A multi-arch image is published with each tagged release. See
    `Dockerfile` at the repo root.

## Verifying the install

```python
import mscompress
print(mscompress.__version__)
```

```js
import { getVersion } from "mscompress";
console.log(getVersion());
```

```bash
mscompress --version
```

All three should report the same version. The single source of truth is
`.cz.toml` at the repo root; `cz bump` updates every consumer.
