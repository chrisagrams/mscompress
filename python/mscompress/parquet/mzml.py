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
import zlib
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Tuple, Union

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
