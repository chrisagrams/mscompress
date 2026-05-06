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
from dataclasses import dataclass
from os import PathLike
from pathlib import Path
from typing import Optional, Tuple, Union

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


# Aliases for the logical columns the loader cares about. First match wins,
# so list canonical names first.
_MZ_ALIASES = ("mz", "m/z", "MZ", "M/Z", "mass_to_charge")
_INTENSITY_ALIASES = ("intensity", "intensities", "int", "Intensity", "INTENSITY")
_PEPTIDE_ALIASES = ("peptide", "Peptide", "sequence")
_CHARGE_ALIASES = ("charge", "Charge", "z", "precursor_charge")
_PEPTIDE_CHARGE_ALIASES = ("peptide_charge", "peptide.charge", "PeptideCharge")
_PRECURSOR_ALIASES = ("precursor", "precursor_mz", "Precursor")
_RET_TIME_ALIASES = ("ret_time", "retention_time", "rt", "RT")
# Auto-resolved score columns when caller passes score_column=None and wants
# annotations. Order = priority.
_SCORE_AUTO_FALLBACKS = ("max_score", "mean_score", "score", "Score")

# Defaults for missing optional columns.
_DEFAULT_RET_TIME = -1.0
_DEFAULT_PRECURSOR = 0.0
_DEFAULT_CHARGE = 0
_DEFAULT_PEPTIDE = ""

_OPTIONAL_FLOAT_COLUMNS = (
    "n_spectra", "mean_score", "max_score", "n_peaks", "total_intensity",
)


@dataclass(frozen=True)
class _ColumnMap:
    """Maps logical fields to actual parquet column names. None = absent."""
    mz: str
    intensity: str
    peptide: Optional[str]
    charge: Optional[str]
    peptide_charge: Optional[str]  # combined "PEPTIDE_2" string column
    precursor: Optional[str]
    ret_time: Optional[str]
    score: Optional[str]


def _first_present(names: Tuple[str, ...], schema_names) -> Optional[str]:
    for n in names:
        if n in schema_names:
            return n
    return None


def _resolve_schema(
    schema: pa.Schema,
    *,
    score_column: Optional[str] = None,
) -> _ColumnMap:
    """Resolve a parquet schema to logical loader columns.

    Required: mz + intensity (under any alias).
    Everything else is optional; missing fields get filled at row time.

    `score_column`: if provided, must exist in the schema or this raises.
    If None, falls back to a known list of common score columns; if none
    of those exist either, score is left unresolved (TSV writes empty cells).
    """
    names = set(schema.names)

    mz = _first_present(_MZ_ALIASES, names)
    intensity = _first_present(_INTENSITY_ALIASES, names)
    if mz is None or intensity is None:
        raise ValueError(
            "parquet missing required mz/intensity columns. "
            f"Looked for mz in {_MZ_ALIASES} and intensity in "
            f"{_INTENSITY_ALIASES}; got schema columns: {schema.names}"
        )

    mz_t = schema.field(mz).type
    if not pa.types.is_list(mz_t):
        raise ValueError(f"`{mz}` must be a list column, got {mz_t}")
    inten_t = schema.field(intensity).type
    if not pa.types.is_list(inten_t):
        raise ValueError(f"`{intensity}` must be a list column, got {inten_t}")

    peptide = _first_present(_PEPTIDE_ALIASES, names)
    charge = _first_present(_CHARGE_ALIASES, names)
    peptide_charge = (
        _first_present(_PEPTIDE_CHARGE_ALIASES, names)
        if peptide is None or charge is None
        else None
    )
    precursor = _first_present(_PRECURSOR_ALIASES, names)
    ret_time = _first_present(_RET_TIME_ALIASES, names)

    if score_column is not None:
        if score_column not in names:
            raise ValueError(
                f"score_column={score_column!r} not in parquet schema {schema.names}"
            )
        score = score_column
    else:
        score = _first_present(_SCORE_AUTO_FALLBACKS, names)

    return _ColumnMap(
        mz=mz,
        intensity=intensity,
        peptide=peptide,
        charge=charge,
        peptide_charge=peptide_charge,
        precursor=precursor,
        ret_time=ret_time,
        score=score,
    )


def _split_peptide_charge(s: str) -> Tuple[str, int]:
    """Split a combined "<peptide>_<charge>" string.

    Splits on the *last* underscore; modifications occasionally produce
    leading/embedded underscores (e.g. `_(Acetyl)PEPTIDE_2`), but the trailing
    charge is always numeric. If the suffix isn't an integer, returns the
    whole string as the peptide and 0 as the charge.
    """
    if not s:
        return _DEFAULT_PEPTIDE, _DEFAULT_CHARGE
    idx = s.rfind("_")
    if idx == -1:
        return s, _DEFAULT_CHARGE
    head, tail = s[:idx], s[idx + 1:]
    try:
        return head, int(tail)
    except ValueError:
        return s, _DEFAULT_CHARGE


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
    cmap = _resolve_schema(pf.schema_arrow)
    total = pf.metadata.num_rows

    columns = [cmap.mz, cmap.intensity]
    for c in (cmap.precursor, cmap.charge, cmap.ret_time, cmap.peptide_charge):
        if c is not None:
            columns.append(c)

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
        for batch in pf.iter_batches(columns=columns):
            mz_col = batch.column(cmap.mz)
            inten_col = batch.column(cmap.intensity)
            prec_col = (
                batch.column(cmap.precursor).to_numpy(zero_copy_only=False)
                if cmap.precursor else None
            )
            charge_col = (
                batch.column(cmap.charge).to_numpy(zero_copy_only=False)
                if cmap.charge else None
            )
            rt_col = (
                batch.column(cmap.ret_time).to_numpy(zero_copy_only=False)
                if cmap.ret_time else None
            )
            pep_charge_col = (
                batch.column(cmap.peptide_charge).to_pylist()
                if cmap.peptide_charge else None
            )

            for i in range(batch.num_rows):
                # mz/intensity arrive as Arrow ListScalar -> numpy float32 view.
                mz_arr = mz_col[i].values.to_numpy(zero_copy_only=False).astype("<f4", copy=False)
                inten_arr = inten_col[i].values.to_numpy(zero_copy_only=False).astype("<f4", copy=False)
                mz_b64 = _encode_binary(mz_arr.tobytes(), use_zlib_binary)
                in_b64 = _encode_binary(inten_arr.tobytes(), use_zlib_binary)

                rt_val = float(rt_col[i]) if rt_col is not None else _DEFAULT_RET_TIME
                prec_val = float(prec_col[i]) if prec_col is not None else _DEFAULT_PRECURSOR
                if charge_col is not None:
                    charge_val = int(charge_col[i])
                elif pep_charge_col is not None:
                    _, charge_val = _split_peptide_charge(pep_charge_col[i] or "")
                else:
                    charge_val = _DEFAULT_CHARGE

                scan_no = idx + 1
                out.write(
                    b'<spectrum index="' + str(idx).encode("ascii")
                    + b'" id="scan=' + str(scan_no).encode("ascii")
                    + b'" defaultArrayLength="' + str(len(mz_arr)).encode("ascii") + b'">'
                    b'<cvParam cvRef="MS" accession="' + _ACC_MS_LEVEL.encode("ascii")
                    + b'" name="ms level" value="2"/>'
                    b'<scanList count="1"><scan>'
                    b'<cvParam cvRef="MS" accession="' + _ACC_RET_TIME.encode("ascii")
                    + b'" name="scan start time" value="' + repr(rt_val).encode("ascii")
                    + b'" unitCvRef="UO" unitAccession="' + unit_acc.encode("ascii")
                    + b'" unitName="' + unit_name.encode("ascii") + b'"/>'
                    b'</scan></scanList>'
                    b'<precursorList count="1"><precursor>'
                    b'<selectedIonList count="1"><selectedIon>'
                    b'<cvParam cvRef="MS" accession="' + _ACC_PRECURSOR_MZ.encode("ascii")
                    + b'" name="selected ion m/z" value="' + repr(prec_val).encode("ascii") + b'"/>'
                    b'<cvParam cvRef="MS" accession="' + _ACC_CHARGE.encode("ascii")
                    + b'" name="charge state" value="' + str(charge_val).encode("ascii") + b'"/>'
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
    score_column: Optional[str],
) -> int:
    """Write a Percolator-compatible TSV beside the spectra file.

    Returns the number of rows written.

    `score_column=None` writes empty `score` cells (callers downstream of
    `TSVReader` will see `score=0.0`). Pass an explicit column name to require
    it; if absent from the schema, `_resolve_schema` raises.
    """
    pf = pq.ParquetFile(str(parquet_path))
    schema = pf.schema_arrow
    cmap = _resolve_schema(schema, score_column=score_column)

    # Optional float columns we still surface as TSV extras when present (and
    # not already used as the score column).
    extra_cols = [
        c for c in _OPTIONAL_FLOAT_COLUMNS
        if c in schema.names and c != cmap.score
    ]
    columns = [c for c in (
        cmap.peptide, cmap.charge, cmap.peptide_charge,
        cmap.precursor, cmap.ret_time, cmap.score,
    ) if c is not None] + extra_cols

    headers = ["ScanNr", "Peptide", "Charge", "score", "precursor", "ret_time"] + extra_cols

    n = 0
    with open(tsv_path, "w", encoding="utf-8", newline="\n") as out:
        out.write("\t".join(headers) + "\n")
        for batch in pf.iter_batches(columns=columns):
            peptide_col = (
                batch.column(cmap.peptide).to_pylist() if cmap.peptide else None
            )
            charge_col = (
                batch.column(cmap.charge).to_numpy(zero_copy_only=False)
                if cmap.charge else None
            )
            pep_charge_col = (
                batch.column(cmap.peptide_charge).to_pylist()
                if cmap.peptide_charge else None
            )
            precursor_col = (
                batch.column(cmap.precursor).to_numpy(zero_copy_only=False)
                if cmap.precursor else None
            )
            ret_time_col = (
                batch.column(cmap.ret_time).to_numpy(zero_copy_only=False)
                if cmap.ret_time else None
            )
            score_col = (
                batch.column(cmap.score).to_numpy(zero_copy_only=False)
                if cmap.score else None
            )
            extras = {
                c: batch.column(c).to_numpy(zero_copy_only=False) for c in extra_cols
            }

            for i in range(batch.num_rows):
                n += 1
                if peptide_col is not None:
                    peptide_val = peptide_col[i] or _DEFAULT_PEPTIDE
                    charge_val = (
                        int(charge_col[i]) if charge_col is not None else _DEFAULT_CHARGE
                    )
                elif pep_charge_col is not None:
                    peptide_val, charge_val = _split_peptide_charge(
                        pep_charge_col[i] or ""
                    )
                else:
                    peptide_val = _DEFAULT_PEPTIDE
                    charge_val = (
                        int(charge_col[i]) if charge_col is not None else _DEFAULT_CHARGE
                    )

                precursor_val = (
                    float(precursor_col[i]) if precursor_col is not None
                    else _DEFAULT_PRECURSOR
                )
                ret_time_val = (
                    float(ret_time_col[i]) if ret_time_col is not None
                    else _DEFAULT_RET_TIME
                )
                score_cell = (
                    repr(float(score_col[i])) if score_col is not None else ""
                )

                row = [
                    str(n),
                    _tsv_escape(peptide_val),
                    str(charge_val),
                    score_cell,
                    repr(precursor_val),
                    repr(ret_time_val),
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
        parquet_path: Source parquet. Must contain `mz` and `intensity`
            list-typed columns (aliases: `m/z`, `MZ`, `mass_to_charge` for mz;
            `int`, `intensities`, `Intensity`, `INTENSITY` for intensity).
            Optional columns: `precursor`, `charge`, `ret_time`, `peptide`,
            and the combined `peptide_charge` ("PEPTIDE_2") string column.
            Missing optional columns are filled with defaults
            (`ret_time=-1.0`, `precursor=0.0`, `charge=0`).
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
    score_column: Optional[str] = None,
) -> Path:
    """Extract per-row metadata from a parquet into a Percolator-style TSV.

    Args:
        parquet_path: Source parquet.
        output_path: Destination `.tsv` path.
        score_column: Parquet column to surface as the PSM `score`. If `None`
            (default), the writer auto-picks one of `max_score`, `mean_score`,
            `score`, or `Score` if present, otherwise leaves the score cells
            empty. If you pass an explicit name and it is not in the schema,
            this raises.

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
    score_column: Optional[str] = None,
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
        score_column: Parquet column to use as PSM score. `None` (default)
            auto-picks `max_score`/`mean_score`/`score`/`Score` if present,
            otherwise emits empty score cells. An explicit name that is not
            in the schema raises.
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
