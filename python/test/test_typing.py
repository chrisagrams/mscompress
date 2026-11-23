"""Static typing checks for the shipped stubs (``_core.pyi``), run through mypy."""

import textwrap

import pytest

mypy_api = pytest.importorskip("mypy.api")

SNIPPET = textwrap.dedent(
    """
    from typing import assert_type
    from mscompress import MSZFile, MSZXFile, MZMLFile


    def mszx(p: str) -> None:
        with MSZXFile.open(p) as m:
            assert_type(m, MSZXFile)
            m.manifest
            out = m.extract("o.msz", [0])
            assert_type(out, MSZXFile)
            out.close()


    def msz(f: MSZFile) -> None:
        with f as m:
            assert_type(m, MSZFile)


    def mzml(f: MZMLFile) -> None:
        with f as m:
            assert_type(m, MZMLFile)
    """
)


def test_context_manager_preserves_subclass(tmp_path):
    src = tmp_path / "check_types.py"
    src.write_text(SNIPPET)
    stdout, stderr, status = mypy_api.run(
        [
            "--strict",
            "--follow-imports=silent",
            "--no-incremental",
            "--cache-dir",
            str(tmp_path / ".mypy_cache"),
            str(src),
        ]
    )
    # Only judge the snippet: the stubs themselves are not --strict clean.
    errors = [line for line in stdout.splitlines() if line.startswith(str(src))]
    assert not errors, stdout + stderr
    assert not stderr, stderr
