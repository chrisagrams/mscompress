"""mzML knowledge used by both directions of the parquet bridge.

Forward (parquet -> mzML synthesis):
    `_synthesize_mzml` writes a minimal mzML file from a parquet, including
    the schema-resolution helpers it leans on (`_ColumnMap`, `_resolve_schema`,
    `_split_peptide_charge`).

Inverse (mzML XML parsing for msz/mszx/mzML -> parquet):
    `_iter_cv_params`, `_extract_precursor_charge`, `_detect_ret_time_unit`
    parse the same cvParams the forward path writes.

Everything else in the parquet package (`msz.py`, `mszx.py`, the package
init's `to_parquet`) depends on this module; this module imports only from
`mscompress._core` / `mscompress.mszx`.
"""

from __future__ import annotations

import base64
import math
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import IO, Optional, Tuple, Union

import numpy as np
import pyarrow as pa
import pyarrow.parquet as pq

from mscompress._core import BaseFile
from mscompress.mszx import MSZXFile


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
_ACC_ION_MOBILITY = "MS:1002476"   # ion mobility drift time
_ACC_ISO_TARGET = "MS:1000827"     # isolation window target m/z
_ACC_ISO_LOWER = "MS:1000828"      # isolation window lower offset
_ACC_ISO_UPPER = "MS:1000829"      # isolation window upper offset
_ACC_UO_MINUTE = "UO:0000031"
_ACC_UO_SECOND = "UO:0000010"
_ACC_UO_MILLISECOND = "UO:0000028"


# Aliases for the logical columns the loader cares about. First match wins,
# so list canonical names first.
_MZ_ALIASES = ("mz", "m/z", "MZ", "M/Z", "mass_to_charge")
_INTENSITY_ALIASES = ("intensity", "intensities", "int", "Intensity", "INTENSITY")
_PEPTIDE_ALIASES = ("peptide", "Peptide", "sequence")
_CHARGE_ALIASES = ("charge", "Charge", "z", "precursor_charge")
_PEPTIDE_CHARGE_ALIASES = ("peptide_charge", "peptide.charge", "PeptideCharge")
_PRECURSOR_ALIASES = ("precursor", "precursor_mz", "Precursor")
_RET_TIME_ALIASES = ("ret_time", "retention_time", "rt", "RT")
# Long-format-only columns (one row per peak; ThermoRawFileParser .mzparquet).
_SCAN_ALIASES = ("scan", "scan_number", "scanNumber", "scan_id")
_LEVEL_ALIASES = ("level", "ms_level", "msLevel")
_ION_MOBILITY_ALIASES = ("ion_mobility", "ionMobility", "drift_time")
_ISOLATION_LOWER_ALIASES = ("isolation_lower", "isolationLower")
_ISOLATION_UPPER_ALIASES = ("isolation_upper", "isolationUpper")
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
    """Maps logical fields to actual parquet column names. None = absent.

    `is_long=True` means one row per peak (long format, e.g.
    ThermoRawFileParser .mzparquet); `mz`/`intensity` are scalar float
    columns and rows must be aggregated by `scan` to form spectra. Otherwise
    each row is one spectrum and `mz`/`intensity` are list columns.
    """
    mz: str
    intensity: str
    peptide: Optional[str]
    charge: Optional[str]
    peptide_charge: Optional[str]  # combined "PEPTIDE_2" string column
    precursor: Optional[str]
    ret_time: Optional[str]
    score: Optional[str]
    scan: Optional[str] = None
    level: Optional[str] = None
    ion_mobility: Optional[str] = None
    isolation_lower: Optional[str] = None
    isolation_upper: Optional[str] = None
    is_long: bool = False


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
    inten_t = schema.field(intensity).type
    mz_is_list = pa.types.is_list(mz_t) or pa.types.is_large_list(mz_t)
    inten_is_list = pa.types.is_list(inten_t) or pa.types.is_large_list(inten_t)
    mz_is_scalar_num = pa.types.is_floating(mz_t) or pa.types.is_integer(mz_t)
    inten_is_scalar_num = pa.types.is_floating(inten_t) or pa.types.is_integer(inten_t)

    if mz_is_list and inten_is_list:
        is_long = False
    elif mz_is_scalar_num and inten_is_scalar_num:
        # Long format requires a scan-id column to aggregate peaks → spectra.
        if _first_present(_SCAN_ALIASES, names) is None:
            raise ValueError(
                f"scalar `{mz}`/`{intensity}` columns require a scan-id column "
                f"(one of {_SCAN_ALIASES}) for long-format aggregation; "
                f"got schema columns: {schema.names}"
            )
        is_long = True
    else:
        raise ValueError(
            f"`{mz}` and `{intensity}` must both be list or both be scalar; "
            f"got mz={mz_t}, intensity={inten_t}"
        )

    peptide = _first_present(_PEPTIDE_ALIASES, names)
    charge = _first_present(_CHARGE_ALIASES, names)
    peptide_charge = (
        _first_present(_PEPTIDE_CHARGE_ALIASES, names)
        if peptide is None or charge is None
        else None
    )
    precursor = _first_present(_PRECURSOR_ALIASES, names)
    ret_time = _first_present(_RET_TIME_ALIASES, names)

    scan_col = _first_present(_SCAN_ALIASES, names) if is_long else None
    level_col = _first_present(_LEVEL_ALIASES, names) if is_long else None
    ion_mobility_col = _first_present(_ION_MOBILITY_ALIASES, names) if is_long else None
    iso_lower_col = _first_present(_ISOLATION_LOWER_ALIASES, names) if is_long else None
    iso_upper_col = _first_present(_ISOLATION_UPPER_ALIASES, names) if is_long else None

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
        scan=scan_col,
        level=level_col,
        ion_mobility=ion_mobility_col,
        isolation_lower=iso_lower_col,
        isolation_upper=iso_upper_col,
        is_long=is_long,
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


def _write_spectrum_xml(
    out: IO[bytes],
    *,
    idx: int,
    scan_no: int,
    ms_level: int,
    rt_val: float,
    mz_arr: np.ndarray,
    inten_arr: np.ndarray,
    unit_acc: str,
    unit_name: str,
    comp_acc: str,
    comp_name: str,
    use_zlib_binary: bool,
    precursor: Optional[Tuple[float, int]] = None,
    ion_mobility: Optional[float] = None,
    isolation_window: Optional[Tuple[float, float, float]] = None,
) -> None:
    """Emit one <spectrum> element to `out`.

    `precursor=None` skips the <precursorList> entirely (MS1 spectra). When
    provided as `(mz, charge)`, emits the standard selectedIon block. When
    `ion_mobility` is set it adds an MS:1002476 cvParam inside <scan>. When
    `isolation_window` is `(target, lower_offset, upper_offset)` it adds an
    <isolationWindow> block inside <precursor>; ignored if `precursor` is None.
    """
    mz_b64 = _encode_binary(mz_arr.tobytes(), use_zlib_binary)
    in_b64 = _encode_binary(inten_arr.tobytes(), use_zlib_binary)

    spectrum_header = (
        b'<spectrum index="' + str(idx).encode("ascii")
        + b'" id="scan=' + str(scan_no).encode("ascii")
        + b'" defaultArrayLength="' + str(len(mz_arr)).encode("ascii") + b'">'
        b'<cvParam cvRef="MS" accession="' + _ACC_MS_LEVEL.encode("ascii")
        + b'" name="ms level" value="' + str(ms_level).encode("ascii") + b'"/>'
    )

    scan_block_parts = [
        b'<scanList count="1"><scan>'
        b'<cvParam cvRef="MS" accession="' + _ACC_RET_TIME.encode("ascii")
        + b'" name="scan start time" value="' + repr(rt_val).encode("ascii")
        + b'" unitCvRef="UO" unitAccession="' + unit_acc.encode("ascii")
        + b'" unitName="' + unit_name.encode("ascii") + b'"/>'
    ]
    if ion_mobility is not None:
        scan_block_parts.append(
            b'<cvParam cvRef="MS" accession="' + _ACC_ION_MOBILITY.encode("ascii")
            + b'" name="ion mobility drift time" value="'
            + repr(float(ion_mobility)).encode("ascii")
            + b'" unitCvRef="UO" unitAccession="' + _ACC_UO_MILLISECOND.encode("ascii")
            + b'" unitName="millisecond"/>'
        )
    scan_block_parts.append(b'</scan></scanList>')
    scan_block = b''.join(scan_block_parts)

    if precursor is not None:
        prec_val, charge_val = precursor
        iso_block = b''
        if isolation_window is not None:
            target, lower_off, upper_off = isolation_window
            iso_block = (
                b'<isolationWindow>'
                b'<cvParam cvRef="MS" accession="' + _ACC_ISO_TARGET.encode("ascii")
                + b'" name="isolation window target m/z" value="'
                + repr(float(target)).encode("ascii") + b'"/>'
                b'<cvParam cvRef="MS" accession="' + _ACC_ISO_LOWER.encode("ascii")
                + b'" name="isolation window lower offset" value="'
                + repr(float(lower_off)).encode("ascii") + b'"/>'
                b'<cvParam cvRef="MS" accession="' + _ACC_ISO_UPPER.encode("ascii")
                + b'" name="isolation window upper offset" value="'
                + repr(float(upper_off)).encode("ascii") + b'"/>'
                b'</isolationWindow>'
            )
        precursor_block = (
            b'<precursorList count="1"><precursor>'
            + iso_block
            + b'<selectedIonList count="1"><selectedIon>'
            b'<cvParam cvRef="MS" accession="' + _ACC_PRECURSOR_MZ.encode("ascii")
            + b'" name="selected ion m/z" value="' + repr(float(prec_val)).encode("ascii") + b'"/>'
            b'<cvParam cvRef="MS" accession="' + _ACC_CHARGE.encode("ascii")
            + b'" name="charge state" value="' + str(int(charge_val)).encode("ascii") + b'"/>'
            b'</selectedIon></selectedIonList>'
            b'</precursor></precursorList>'
        )
    else:
        precursor_block = b''

    binary_block = (
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
        # trailing newline: extract_spectrum_last_xml reads spectrum_end+1,
        # expecting whitespace.
        b'</spectrum>\n'
    )

    out.write(spectrum_header + scan_block + precursor_block + binary_block)


def _count_long_format_spectra(pf: pq.ParquetFile, scan_col: str) -> int:
    """Count unique scans across all row groups.

    Counts run transitions per batch plus the boundary between batches; for a
    well-formed mzparquet (writer invariant: a scan never spans row groups)
    this equals the spectrum count. The cross-batch carry guards against a
    malformed source silently inflating the count.
    """
    total = 0
    prev_last_scan: Optional[int] = None
    for batch in pf.iter_batches(columns=[scan_col]):
        scans = batch.column(scan_col).to_numpy(zero_copy_only=False)
        if len(scans) == 0:
            continue
        n_runs = 1 + int(np.count_nonzero(scans[1:] != scans[:-1]))
        if prev_last_scan is not None and int(scans[0]) == prev_last_scan:
            n_runs -= 1
        total += n_runs
        prev_last_scan = int(scans[-1])
    return total


def _synthesize_mzml_wide(
    out: IO[bytes],
    pf: pq.ParquetFile,
    cmap: _ColumnMap,
    *,
    unit_acc: str,
    unit_name: str,
    comp_acc: str,
    comp_name: str,
    use_zlib_binary: bool,
) -> int:
    """Wide-format iterator: one parquet row → one spectrum.

    `cmap.mz` and `cmap.intensity` are list columns; metadata is per-row.
    """
    columns = [cmap.mz, cmap.intensity]
    for c in (cmap.precursor, cmap.charge, cmap.ret_time, cmap.peptide_charge):
        if c is not None:
            columns.append(c)

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
            mz_arr = mz_col[i].values.to_numpy(zero_copy_only=False).astype("<f4", copy=False)
            inten_arr = inten_col[i].values.to_numpy(zero_copy_only=False).astype("<f4", copy=False)

            rt_val = float(rt_col[i]) if rt_col is not None else _DEFAULT_RET_TIME
            prec_val = float(prec_col[i]) if prec_col is not None else _DEFAULT_PRECURSOR
            if charge_col is not None:
                charge_val = int(charge_col[i])
            elif pep_charge_col is not None:
                _, charge_val = _split_peptide_charge(pep_charge_col[i] or "")
            else:
                charge_val = _DEFAULT_CHARGE

            _write_spectrum_xml(
                out,
                idx=idx,
                scan_no=idx + 1,
                ms_level=2,
                rt_val=rt_val,
                mz_arr=mz_arr,
                inten_arr=inten_arr,
                unit_acc=unit_acc,
                unit_name=unit_name,
                comp_acc=comp_acc,
                comp_name=comp_name,
                use_zlib_binary=use_zlib_binary,
                precursor=(prec_val, charge_val),
            )
            idx += 1
    return idx


@dataclass
class _LongSpectrum:
    """One scan's worth of aggregated long-format rows, before mzML conversion."""
    scan: int
    level: int
    mz: list
    intensity: list
    rt: Optional[float]
    precursor_mz: Optional[float]
    precursor_charge: Optional[int]
    ion_mobility: Optional[float]
    isolation_lower: Optional[float]
    isolation_upper: Optional[float]


def _iter_long_spectra(pf: pq.ParquetFile, cmap: _ColumnMap):
    """Yield `_LongSpectrum` per unique scan from a long-format parquet.

    Relies on the ThermoRawFileParser writer invariant that all peaks for a
    single scan land in the same row group. A defensive cross-batch carry
    handles the case where that invariant is violated (e.g. by a malformed
    source); without it, two adjacent batches sharing a scan would be emitted
    as two spectra.
    """
    assert cmap.scan is not None and cmap.level is not None
    columns = [cmap.mz, cmap.intensity, cmap.scan, cmap.level]
    for c in (
        cmap.ret_time, cmap.precursor, cmap.charge,
        cmap.ion_mobility, cmap.isolation_lower, cmap.isolation_upper,
    ):
        if c is not None:
            columns.append(c)

    def _scalar_or_none(arr, i):
        """`arr[i].as_py()` semantics on a pyarrow Array (returns None for null)."""
        return arr[i].as_py() if arr is not None else None

    pending: Optional[_LongSpectrum] = None  # carries an open run across batch boundaries

    for batch in pf.iter_batches(columns=columns):
        if batch.num_rows == 0:
            continue

        scans = batch.column(cmap.scan).to_numpy(zero_copy_only=False)
        levels = batch.column(cmap.level).to_numpy(zero_copy_only=False)
        mz_vals = batch.column(cmap.mz).to_numpy(zero_copy_only=False).astype("<f4", copy=False)
        in_vals = batch.column(cmap.intensity).to_numpy(zero_copy_only=False).astype("<f4", copy=False)

        # Nullable columns: keep as pyarrow Arrays for None-aware indexing.
        rt_arr = batch.column(cmap.ret_time) if cmap.ret_time else None
        prec_arr = batch.column(cmap.precursor) if cmap.precursor else None
        charge_arr = batch.column(cmap.charge) if cmap.charge else None
        im_arr = batch.column(cmap.ion_mobility) if cmap.ion_mobility else None
        iso_lo_arr = batch.column(cmap.isolation_lower) if cmap.isolation_lower else None
        iso_hi_arr = batch.column(cmap.isolation_upper) if cmap.isolation_upper else None

        run_starts = np.concatenate(
            ([0], np.flatnonzero(scans[1:] != scans[:-1]) + 1)
        ).astype(np.int64)
        run_ends = np.concatenate(
            (run_starts[1:], [len(scans)])
        ).astype(np.int64)

        for run_start, run_end in zip(run_starts, run_ends):
            run_start = int(run_start)
            run_end = int(run_end)
            scan_no = int(scans[run_start])

            if pending is not None and pending.scan == scan_no:
                # Carry-over: this batch resumes the previous batch's open run.
                pending.mz.extend(mz_vals[run_start:run_end].tolist())
                pending.intensity.extend(in_vals[run_start:run_end].tolist())
                continue

            if pending is not None:
                yield pending

            pending = _LongSpectrum(
                scan=scan_no,
                level=int(levels[run_start]),
                mz=mz_vals[run_start:run_end].tolist(),
                intensity=in_vals[run_start:run_end].tolist(),
                rt=_scalar_or_none(rt_arr, run_start),
                precursor_mz=_scalar_or_none(prec_arr, run_start),
                precursor_charge=_scalar_or_none(charge_arr, run_start),
                ion_mobility=_scalar_or_none(im_arr, run_start),
                isolation_lower=_scalar_or_none(iso_lo_arr, run_start),
                isolation_upper=_scalar_or_none(iso_hi_arr, run_start),
            )

    if pending is not None:
        yield pending


def _emit_long_spectrum(
    out: IO[bytes],
    *,
    idx: int,
    spec: _LongSpectrum,
    unit_acc: str,
    unit_name: str,
    comp_acc: str,
    comp_name: str,
    use_zlib_binary: bool,
) -> None:
    """Serialize one aggregated long-format spectrum as an mzML <spectrum>."""
    rt_val = spec.rt
    if rt_val is None or (isinstance(rt_val, float) and math.isnan(rt_val)):
        rt_val = _DEFAULT_RET_TIME

    if spec.level == 1:
        precursor = None
        isolation_window = None
    else:
        precursor = (
            float(spec.precursor_mz) if spec.precursor_mz is not None else _DEFAULT_PRECURSOR,
            int(spec.precursor_charge) if spec.precursor_charge is not None else _DEFAULT_CHARGE,
        )
        if spec.isolation_lower is not None and spec.isolation_upper is not None:
            target = (float(spec.isolation_lower) + float(spec.isolation_upper)) / 2.0
            isolation_window = (
                target,
                target - float(spec.isolation_lower),
                float(spec.isolation_upper) - target,
            )
        else:
            isolation_window = None

    _write_spectrum_xml(
        out,
        idx=idx,
        scan_no=int(spec.scan),
        ms_level=int(spec.level),
        rt_val=float(rt_val),
        mz_arr=np.asarray(spec.mz, dtype="<f4"),
        inten_arr=np.asarray(spec.intensity, dtype="<f4"),
        unit_acc=unit_acc,
        unit_name=unit_name,
        comp_acc=comp_acc,
        comp_name=comp_name,
        use_zlib_binary=use_zlib_binary,
        precursor=precursor,
        ion_mobility=float(spec.ion_mobility) if spec.ion_mobility is not None else None,
        isolation_window=isolation_window,
    )


def _synthesize_mzml_long(
    out: IO[bytes],
    pf: pq.ParquetFile,
    cmap: _ColumnMap,
    *,
    unit_acc: str,
    unit_name: str,
    comp_acc: str,
    comp_name: str,
    use_zlib_binary: bool,
) -> int:
    """Aggregate long-format rows into spectra and stream them as mzML."""
    count = 0
    for idx, spec in enumerate(_iter_long_spectra(pf, cmap)):
        _emit_long_spectrum(
            out,
            idx=idx,
            spec=spec,
            unit_acc=unit_acc,
            unit_name=unit_name,
            comp_acc=comp_acc,
            comp_name=comp_name,
            use_zlib_binary=use_zlib_binary,
        )
        count = idx + 1
    return count


def _synthesize_mzml(
    parquet_path: Path,
    mzml_path: Path,
    *,
    ret_time_unit: str,
    use_zlib_binary: bool,
) -> int:
    """Stream a parquet file into a minimal mzML on disk.

    Dispatches to the wide- or long-format iterator based on the schema.
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

    if cmap.is_long:
        assert cmap.scan is not None
        total = _count_long_format_spectra(pf, cmap.scan)
    else:
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

        if cmap.is_long:
            idx = _synthesize_mzml_long(
                out, pf, cmap,
                unit_acc=unit_acc, unit_name=unit_name,
                comp_acc=comp_acc, comp_name=comp_name,
                use_zlib_binary=use_zlib_binary,
            )
        else:
            idx = _synthesize_mzml_wide(
                out, pf, cmap,
                unit_acc=unit_acc, unit_name=unit_name,
                comp_acc=comp_acc, comp_name=comp_name,
                use_zlib_binary=use_zlib_binary,
            )

        out.write(b'</spectrumList></run></mzML>')

    return idx


# ---------------------------------------------------------------------------
# Inverse-path helpers: parsing mzML-style cvParams out of spectrum XML.
# These read the same cvParams the forward path writes, so changes to either
# side should track each other.
# ---------------------------------------------------------------------------


def _iter_cv_params(xml_elem):
    """Yield every cvParam descendant, namespace-agnostic."""
    if xml_elem is None:
        return
    for elem in xml_elem.iter():
        tag = elem.tag
        # ElementTree puts namespaces in `{uri}local` form; bare `cvParam`
        # appears when the source XML has no default xmlns (e.g. the parquet
        # synthesizer's output).
        if tag == "cvParam" or (
            isinstance(tag, str) and tag.endswith("}cvParam")
        ):
            yield elem


def _extract_precursor_charge(xml_elem) -> Tuple[float, int]:
    """Pull precursor m/z (MS:1000744) and charge (MS:1000041) from spectrum XML.

    Missing values fall back to the same defaults the forward path uses, so a
    parquet -> msz -> parquet round-trip is stable on rows that originally had
    no precursor/charge.
    """
    precursor = _DEFAULT_PRECURSOR
    charge = _DEFAULT_CHARGE
    for elem in _iter_cv_params(xml_elem):
        acc = elem.attrib.get("accession", "")
        if acc == _ACC_PRECURSOR_MZ:
            try:
                precursor = float(elem.attrib.get("value", precursor))
            except (TypeError, ValueError):
                pass
        elif acc == _ACC_CHARGE:
            try:
                charge = int(elem.attrib.get("value", charge))
            except (TypeError, ValueError):
                pass
    return precursor, charge


def _detect_ret_time_unit(file: Union[BaseFile, MSZXFile]) -> str:
    """Inspect the first spectrum's XML to recover the *original* rt unit.

    The C preprocessor normalizes retention time to seconds, so `Spectrum.
    retention_time` is always in seconds regardless of source. The original
    unit only survives on the spectrum XML's `MS:1000016` cvParam. Returns
    `"minute"` when that cvParam carries `UO:0000031`, else `"second"` (the
    safe default when there are no spectra or no unit annotation).
    """
    spectra = file.spectra
    if len(spectra) == 0:
        return "second"
    try:
        xml = spectra[0].xml
    except Exception:
        return "second"
    for elem in _iter_cv_params(xml):
        if elem.attrib.get("accession") != _ACC_RET_TIME:
            continue
        return "minute" if elem.attrib.get("unitAccession") == _ACC_UO_MINUTE else "second"
    return "second"
