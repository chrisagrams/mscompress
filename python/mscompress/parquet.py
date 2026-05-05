"""
Parquet → MSZ / MSZX converter.

Converts a peptide-level consensus parquet (one row per peptide-spectrum entry,
with `mz` / `intensity` list columns) into a compressed `.msz` spectra file and,
optionally, an `.mszx` archive bundling a Percolator-style annotations TSV.

Public API:
    parquet_to_msz                  - parquet -> .msz
    parquet_to_annotations_tsv      - parquet -> .tsv
    parquet_to_mszx                 - parquet -> .mszx (msz + tsv bundle)

Requires the `[parquet]` extra (`pyarrow`).
"""

from __future__ import annotations

import base64
import os
import tempfile
import zlib
from os import PathLike
from pathlib import Path
from typing import Optional, Union

try:
    import pyarrow as pa
    import pyarrow.parquet as pq
except ImportError as e:
    raise ImportError(
        "mscompress.parquet requires pyarrow. Install with: "
        "pip install 'mscompress[parquet]'"
    ) from e

from mscompress._core import MSZFile, MZMLFile
from mscompress.annotations import TSVReader
from mscompress.mszx import MSZXBuilder


# MS / UO accessions used in the synthesized mzML.
_ACC_MS_LEVEL = "MS:1000511"
_ACC_RET_TIME = "MS:1000016"
_ACC_FLOAT32 = "MS:1000521"
_ACC_NO_COMP = "MS:1000576"
_ACC_ZLIB = "MS:1000574"
_ACC_MZ_ARRAY = "MS:1000514"
_ACC_INTEN_ARRAY = "MS:1000515"
_ACC_PRECURSOR_MZ = "MS:1000744"
_ACC_CHARGE = "MS:1000041"
_ACC_UO_MINUTE = "UO:0000031"
_ACC_UO_SECOND = "UO:0000010"


_REQUIRED_COLUMNS = (
    "peptide", "charge", "mz", "intensity", "precursor", "ret_time",
)
_OPTIONAL_FLOAT_COLUMNS = (
    "n_spectra", "mean_score", "max_score", "n_peaks", "total_intensity",
)


def _validate_schema(schema: pa.Schema) -> None:
    missing = [c for c in _REQUIRED_COLUMNS if c not in schema.names]
    if missing:
        raise ValueError(
            f"parquet missing required columns: {missing}. "
            f"Got: {schema.names}"
        )
    mz_t = schema.field("mz").type
    if not pa.types.is_list(mz_t):
        raise ValueError(f"`mz` must be a list column, got {mz_t}")
    inten_t = schema.field("intensity").type
    if not pa.types.is_list(inten_t):
        raise ValueError(f"`intensity` must be a list column, got {inten_t}")


def _encode_binary(values: bytes, use_zlib: bool) -> bytes:
    """Optionally zlib-deflate, then base64-encode raw float bytes."""
    if use_zlib:
        values = zlib.compress(values)
    return base64.b64encode(values)


def _synthesize_mzml(
    parquet_path: Path,
    mzml_path: Path,
    *,
    ret_time_unit: str,
    use_zlib_binary: bool,
) -> int:
    """Stream a parquet file into a minimal mzML on disk.

    Returns the number of spectra written.
    """
    if ret_time_unit == "minute":
        unit_acc, unit_name = _ACC_UO_MINUTE, "minute"
    elif ret_time_unit == "second":
        unit_acc, unit_name = _ACC_UO_SECOND, "second"
    else:
        raise ValueError(f"ret_time_unit must be 'minute' or 'second', got {ret_time_unit!r}")

    comp_acc = _ACC_ZLIB if use_zlib_binary else _ACC_NO_COMP
    comp_name = "zlib compression" if use_zlib_binary else "no compression"

    pf = pq.ParquetFile(str(parquet_path))
    _validate_schema(pf.schema_arrow)
    total = pf.metadata.num_rows

    with open(mzml_path, "wb") as out:
        # NOTE: the cvList preamble is required, not cosmetic. The C
        # `pattern_detect` parser only writes attribute values into its scratch
        # buffer while inside a <cvParam>; without a leading cvParam the
        # `<spectrumList count="N">` value reads back as 0 and downstream
        # division sizing collapses to nothing.
        out.write(
            b'<?xml version="1.0" encoding="utf-8"?>'
            b'<mzML xmlns="http://psi.hupo.org/ms/mzml" version="1.1.0" id="parquet">'
            b'<cvList count="1">'
            b'<cv id="MS" fullName="PSI-MS" version="4.1" URI="https://www.psidev.info/groups/proteomics-standards-initiative-mass-spectrometry-cv"/>'
            b'</cvList>'
            b'<fileDescription><fileContent>'
            b'<cvParam cvRef="MS" accession="MS:1000580" name="MSn spectrum" value=""/>'
            b'</fileContent></fileDescription>'
            b'<run id="parquet">'
            b'<spectrumList count="' + str(total).encode("ascii") + b'">'
        )

        idx = 0
        for batch in pf.iter_batches(
            columns=["mz", "intensity", "precursor", "charge", "ret_time"]
        ):
            mz_col = batch.column("mz")
            inten_col = batch.column("intensity")
            prec_col = batch.column("precursor").to_numpy(zero_copy_only=False)
            charge_col = batch.column("charge").to_numpy(zero_copy_only=False)
            rt_col = batch.column("ret_time").to_numpy(zero_copy_only=False)

            for i in range(batch.num_rows):
                # mz/intensity arrive as Arrow ListScalar -> numpy float32 view.
                mz_arr = mz_col[i].values.to_numpy(zero_copy_only=False).astype("<f4", copy=False)
                inten_arr = inten_col[i].values.to_numpy(zero_copy_only=False).astype("<f4", copy=False)
                mz_b64 = _encode_binary(mz_arr.tobytes(), use_zlib_binary)
                in_b64 = _encode_binary(inten_arr.tobytes(), use_zlib_binary)

                scan_no = idx + 1
                out.write(
                    b'<spectrum index="' + str(idx).encode("ascii")
                    + b'" id="scan=' + str(scan_no).encode("ascii")
                    + b'" defaultArrayLength="' + str(len(mz_arr)).encode("ascii") + b'">'
                    b'<cvParam cvRef="MS" accession="' + _ACC_MS_LEVEL.encode("ascii")
                    + b'" name="ms level" value="2"/>'
                    b'<scanList count="1"><scan>'
                    b'<cvParam cvRef="MS" accession="' + _ACC_RET_TIME.encode("ascii")
                    + b'" name="scan start time" value="' + repr(float(rt_col[i])).encode("ascii")
                    + b'" unitCvRef="UO" unitAccession="' + unit_acc.encode("ascii")
                    + b'" unitName="' + unit_name.encode("ascii") + b'"/>'
                    b'</scan></scanList>'
                    b'<precursorList count="1"><precursor>'
                    b'<selectedIonList count="1"><selectedIon>'
                    b'<cvParam cvRef="MS" accession="' + _ACC_PRECURSOR_MZ.encode("ascii")
                    + b'" name="selected ion m/z" value="' + repr(float(prec_col[i])).encode("ascii") + b'"/>'
                    b'<cvParam cvRef="MS" accession="' + _ACC_CHARGE.encode("ascii")
                    + b'" name="charge state" value="' + str(int(charge_col[i])).encode("ascii") + b'"/>'
                    b'</selectedIon></selectedIonList>'
                    b'</precursor></precursorList>'
                    b'<binaryDataArrayList count="2">'
                    b'<binaryDataArray encodedLength="' + str(len(mz_b64)).encode("ascii") + b'">'
                    b'<cvParam cvRef="MS" accession="' + _ACC_FLOAT32.encode("ascii") + b'" name="32-bit float"/>'
                    b'<cvParam cvRef="MS" accession="' + comp_acc.encode("ascii") + b'" name="' + comp_name.encode("ascii") + b'"/>'
                    b'<cvParam cvRef="MS" accession="' + _ACC_MZ_ARRAY.encode("ascii") + b'" name="m/z array"/>'
                    b'<binary>' + mz_b64 + b'</binary>'
                    b'</binaryDataArray>'
                    b'<binaryDataArray encodedLength="' + str(len(in_b64)).encode("ascii") + b'">'
                    b'<cvParam cvRef="MS" accession="' + _ACC_FLOAT32.encode("ascii") + b'" name="32-bit float"/>'
                    b'<cvParam cvRef="MS" accession="' + comp_acc.encode("ascii") + b'" name="' + comp_name.encode("ascii") + b'"/>'
                    b'<cvParam cvRef="MS" accession="' + _ACC_INTEN_ARRAY.encode("ascii") + b'" name="intensity array"/>'
                    b'<binary>' + in_b64 + b'</binary>'
                    b'</binaryDataArray>'
                    b'</binaryDataArrayList>'
                    b'</spectrum>\n'  # trailing newline: extract_spectrum_last_xml
                                       # reads spectrum_end+1, expecting whitespace.
                )
                idx += 1

        out.write(b'</spectrumList></run></mzML>')

    return idx


def _tsv_escape(value: object) -> str:
    """Render a TSV cell, replacing whitespace that would break the row."""
    if value is None:
        return ""
    s = str(value)
    return s.replace("\t", " ").replace("\n", " ").replace("\r", " ")


def _write_annotations_tsv(
    parquet_path: Path,
    tsv_path: Path,
    *,
    score_column: str,
) -> int:
    """Write a Percolator-compatible TSV beside the spectra file.

    Returns the number of rows written.
    """
    pf = pq.ParquetFile(str(parquet_path))
    schema = pf.schema_arrow
    _validate_schema(schema)

    if score_column not in schema.names:
        raise ValueError(
            f"score_column={score_column!r} not in parquet schema {schema.names}"
        )

    # Columns we materialize per-row. Skip the heavy list columns.
    extra_cols = [
        c for c in _OPTIONAL_FLOAT_COLUMNS
        if c in schema.names and c != score_column
    ]
    columns = ["peptide", "charge", "precursor", "ret_time", score_column] + extra_cols

    headers = ["ScanNr", "Peptide", "Charge", "score", "precursor", "ret_time"] + extra_cols

    n = 0
    with open(tsv_path, "w", encoding="utf-8", newline="\n") as out:
        out.write("\t".join(headers) + "\n")
        for batch in pf.iter_batches(columns=columns):
            peptide = batch.column("peptide").to_pylist()
            charge = batch.column("charge").to_numpy(zero_copy_only=False)
            precursor = batch.column("precursor").to_numpy(zero_copy_only=False)
            ret_time = batch.column("ret_time").to_numpy(zero_copy_only=False)
            score = batch.column(score_column).to_numpy(zero_copy_only=False)
            extras = {c: batch.column(c).to_numpy(zero_copy_only=False) for c in extra_cols}

            for i in range(batch.num_rows):
                n += 1
                row = [
                    str(n),
                    _tsv_escape(peptide[i]),
                    str(int(charge[i])),
                    repr(float(score[i])),
                    repr(float(precursor[i])),
                    repr(float(ret_time[i])),
                ]
                for c in extra_cols:
                    row.append(repr(float(extras[c][i])))
                out.write("\t".join(row) + "\n")

    return n


def parquet_to_msz(
    parquet_path: Union[str, PathLike],
    output_path: Union[str, PathLike],
    *,
    use_zlib_binary: bool = False,
    ret_time_unit: str = "minute",
) -> MSZFile:
    """Convert a peptide-level parquet file into a `.msz` file.

    Args:
        parquet_path: Source parquet (must contain mz / intensity / precursor /
            charge / ret_time / peptide columns; all floats are expected to be
            32-bit).
        output_path: Destination `.msz` path.
        use_zlib_binary: If True, deflate each binary array before base64
            encoding (`MS:1000574`). Default False (`MS:1000576`).
        ret_time_unit: Unit of the parquet `ret_time` column. `"minute"`
            (default) maps to UO:0000031; `"second"` to UO:0000010.

    Returns:
        An open MSZFile for the written output.
    """
    parquet_path = Path(os.fspath(parquet_path))
    output_path = Path(os.fspath(output_path))

    with tempfile.TemporaryDirectory(prefix="mscompress-parquet-") as td:
        tmp_mzml = Path(td) / "synthesized.mzML"
        _synthesize_mzml(
            parquet_path,
            tmp_mzml,
            ret_time_unit=ret_time_unit,
            use_zlib_binary=use_zlib_binary,
        )
        with MZMLFile(str(tmp_mzml).encode("utf-8")) as mzml:
            return mzml.compress(str(output_path))


def parquet_to_annotations_tsv(
    parquet_path: Union[str, PathLike],
    output_path: Union[str, PathLike],
    *,
    score_column: str = "max_score",
) -> Path:
    """Extract per-row metadata from a parquet into a Percolator-style TSV.

    Args:
        parquet_path: Source parquet.
        output_path: Destination `.tsv` path.
        score_column: Parquet column to surface as the PSM `score`
            (default `max_score`).

    Returns:
        The written TSV path.
    """
    parquet_path = Path(os.fspath(parquet_path))
    output_path = Path(os.fspath(output_path))
    _write_annotations_tsv(parquet_path, output_path, score_column=score_column)
    return output_path


def parquet_to_mszx(
    parquet_path: Union[str, PathLike],
    output_path: Union[str, PathLike],
    *,
    score_column: str = "max_score",
    use_zlib_binary: bool = False,
    ret_time_unit: str = "minute",
    description: Optional[str] = None,
) -> Path:
    """Bundle a parquet file into a complete `.mszx` archive.

    Produces a `.mszx` containing:
      - `spectra.msz` - compressed mz/intensity/retention-time data
      - `<source>.tsv.zst` - Percolator-style annotations (peptide/charge/score…)
      - `manifest.json`

    Args:
        parquet_path: Source parquet.
        output_path: Destination `.mszx` path.
        score_column: Parquet column to use as PSM score (default `max_score`).
        use_zlib_binary: zlib-deflate binary arrays inside the synthesized
            mzML before base64 encoding.
        ret_time_unit: `"minute"` (default) or `"second"`.
        description: Optional human-readable description.

    Returns:
        The written archive path.
    """
    parquet_path = Path(os.fspath(parquet_path))
    output_path = Path(os.fspath(output_path))

    with tempfile.TemporaryDirectory(prefix="mscompress-parquet-mszx-") as td_str:
        td = Path(td_str)
        tmp_msz = td / "spectra.msz"
        tmp_tsv = td / (parquet_path.stem + ".tsv")

        msz = parquet_to_msz(
            parquet_path, tmp_msz,
            use_zlib_binary=use_zlib_binary,
            ret_time_unit=ret_time_unit,
        )
        try:
            _write_annotations_tsv(parquet_path, tmp_tsv, score_column=score_column)
            reader = TSVReader(tmp_tsv)
            builder = MSZXBuilder(msz, source_name=parquet_path.name)
            builder.add_annotations(
                reader,
                description=description or f"Annotations extracted from {parquet_path.name}",
            )
            if description:
                builder.set_description(description)
            return builder.save(output_path)
        finally:
            # Release mmap before TemporaryDirectory cleans up tmp_msz (Windows).
            msz.__exit__(None, None, None)
