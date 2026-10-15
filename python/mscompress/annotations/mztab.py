"""
mzTab 1.0 reader and writer.

A :class:`PSMTable` is stored as an mzTab 1.0 file (Summary mode,
Identification type, PSM section only): ``MTD`` lines carry the
:class:`SearchMetadata` and the ``PSH``/``PSM`` lines are the table. Core
columns map to their mzTab equivalents; any other column ``x`` is stored as
``opt_global_x`` and read back under its own name.
"""

from __future__ import annotations

import csv
import io
import re
from os import PathLike, fspath
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Union

import numpy as np
import pandas as pd

from mscompress.annotations import mods
from mscompress.annotations._io import Source, load
from mscompress.annotations.metadata import ModificationSpec, SearchMetadata
from mscompress.annotations.table import PSM_COLUMNS, PSMTable

NULL = "null"
OPT_PREFIX = "opt_global_"
DECOY_COLUMN = "opt_global_cv_MS:1002217_decoy_peptide"

# A CV/user parameter: (cv label, accession, name, value).
Param = Tuple[str, str, str, str]

# mzTab PSM columns, in the order written. Values are the PSMTable column
# each is filled from (None: derived, or left null).
_PSH: List[Tuple[str, Optional[str]]] = [
    ("sequence", "peptide"),
    ("PSM_ID", None),
    ("accession", "proteins"),
    ("unique", None),
    ("database", None),
    ("database_version", None),
    ("search_engine", None),
    ("search_engine_score[1]", "score"),
    ("search_engine_score[2]", "q_value"),
    ("search_engine_score[3]", "pep"),
    ("modifications", "modifications"),
    ("retention_time", "retention_time"),
    ("charge", "charge"),
    ("exp_mass_to_charge", "precursor_mz"),
    ("calc_mass_to_charge", "calc_mz"),
    ("spectra_ref", "scan"),
    ("pre", "pre"),
    ("post", "post"),
    ("start", None),
    ("end", None),
    (DECOY_COLUMN, "is_decoy"),
]
# Columns read straight back into a PSMTable column of another name.
_RENAMES = {
    mztab: col for mztab, col in _PSH
    if col is not None and col not in ("scan", "proteins", "is_decoy")
    and not mztab.startswith("search_engine_score")
}

# Search engines with their own PSI-MS term, keyed by _software_key(name).
_SOFTWARE_TERMS: Dict[str, Tuple[str, str]] = {
    "percolator": ("MS:1001490", "Percolator"),
    "xtandem": ("MS:1001476", "X!Tandem"),
    "msfragger": ("MS:1003014", "MSFragger"),
    "comet": ("MS:1002251", "Comet"),
    "mascot": ("MS:1001207", "Mascot"),
    "sequest": ("MS:1001208", "SEQUEST"),
    "msgf": ("MS:1002048", "MS-GF+"),
    "myrimatch": ("MS:1001585", "MyriMatch"),
    "omssa": ("MS:1001475", "OMSSA"),
    "sage": ("MS:1004007", "Sage"),
    "maxquant": ("MS:1001583", "MaxQuant"),
    "andromeda": ("MS:1002337", "Andromeda"),
    "peptideprophet": ("MS:1002287", "PeptideProphet"),
    "byonic": ("MS:1002261", "Byonic"),
    "metamorpheus": ("MS:1002826", "MetaMorpheus"),
    "pfind": ("MS:1003011", "pFind"),
    "prolucid": ("MS:1002596", "ProLuCID"),
    "tide": ("MS:1002575", "Tide"),
}
_SOFTWARE_BY_ACCESSION = {acc: name for acc, name in _SOFTWARE_TERMS.values()}
_GENERIC_SOFTWARE: Param = ("MS", "MS:1001456", "analysis software", "")

# Primary score name (lowercase) -> CV param.
_SCORE_PARAMS: Dict[str, Param] = {
    "hyperscore": ("MS", "MS:1001331", "X!Tandem:hyperscore", ""),
    "expect": ("MS", "MS:1001330", "X!Tandem:expect", ""),
    "xcorr": ("MS", "MS:1001155", "SEQUEST:xcorr", ""),
}
_GENERIC_SCORE: Param = ("MS", "MS:1001153", "search engine specific score", "")
_SCORE_BY_ACCESSION: Dict[str, Optional[str]] = {
    "MS:1001331": "hyperscore",
    "MS:1001330": "expect",
    "MS:1001155": "xcorr",
    "MS:1001492": "score",
    "MS:1001153": None,
}
_Q_VALUE_ACCESSIONS = {"MS:1001491", "MS:1002354", "MS:1001364"}
_PEP_ACCESSIONS = {"MS:1001493"}

_NO_FIXED_MODS: Param = ("MS", "MS:1002453", "No fixed modifications searched", "")
_NO_VARIABLE_MODS: Param = ("MS", "MS:1002454", "No variable modifications searched", "")

_INDEXED_KEY = re.compile(r"^([A-Za-z_]+)\[(\d+)\](?:-(.+))?$")


def _clean(value: str) -> str:
    """Replace characters that would break a TSV cell."""
    return re.sub(r"[\t\r\n]", " ", value)


def _fmt_param(param: Param) -> str:
    return "[" + ", ".join(_clean(part) for part in param) + "]"


def _parse_param(text: str) -> Optional[Param]:
    text = text.strip()
    if not (text.startswith("[") and text.endswith("]")):
        return None
    parts = [p.strip() for p in text[1:-1].split(",", 3)]
    return (parts[0], parts[1], parts[2], parts[3]) if len(parts) == 4 else None


def _software_key(name: str) -> str:
    return re.sub(r"[^a-z0-9]", "", name.lower())


def _software_param(md: SearchMetadata) -> Param:
    """Software as its PSI-MS term, with the version in the value slot."""
    version = md.software_version or ""
    if not md.software:
        return _GENERIC_SOFTWARE[:3] + (version,)
    term = _SOFTWARE_TERMS.get(_software_key(md.software))
    if term is None:
        return ("", "", md.software, version)
    return ("MS", term[0], term[1], version)


def _score_params(md: SearchMetadata) -> List[Param]:
    percolator = _software_key(md.software or "") == "percolator"
    name = (md.primary_score or "").lower()
    if not name:
        primary = _GENERIC_SCORE
    elif name == "score" and percolator:
        primary = ("MS", "MS:1001492", "percolator:score", "")
    else:
        primary = _SCORE_PARAMS.get(name, ("", "", md.primary_score or "", ""))
    if percolator:
        return [primary, ("MS", "MS:1001491", "percolator:Q value", ""),
                ("MS", "MS:1001493", "percolator:PEP", "")]
    return [primary, ("MS", "MS:1002354", "PSM-level q-value", ""),
            ("", "", "posterior error probability", "")]


def _mod_param(spec: ModificationSpec) -> Param:
    hit = mods.unimod(spec.massdiff, spec.site)
    if hit:
        return ("UNIMOD", f"UNIMOD:{hit[0]}", hit[1], "")
    accession = mods.chemmod(spec.massdiff)
    # The name repeats the accession: jmzTab rejects an empty name.
    return ("CHEMMOD", accession, accession, "")


# -- writer ----------------------------------------------------------------


def _metadata_lines(md: SearchMetadata) -> List[str]:
    lines: List[Tuple[str, str]] = [
        ("mzTab-version", "1.0.0"),
        ("mzTab-mode", "Summary"),
        ("mzTab-type", "Identification"),
    ]
    if md.source_format:
        description = f"Converted by mscompress from {md.source_format}"
        if md.source_file:
            description += f" ({md.source_file})"
    else:
        description = "PSMs written by mscompress"
    lines.append(("description", description))
    lines.append(("ms_run[1]-location",
                  f"file://{md.ms_run_location}" if md.ms_run_location else NULL))
    lines.append(("software[1]", _fmt_param(_software_param(md))))
    for n, param in enumerate(_score_params(md), start=1):
        lines.append((f"psm_search_engine_score[{n}]", _fmt_param(param)))
    for kind, specs, none in (("fixed_mod", md.fixed_mods, _NO_FIXED_MODS),
                              ("variable_mod", md.variable_mods, _NO_VARIABLE_MODS)):
        if not specs:
            lines.append((f"{kind}[1]", _fmt_param(none)))
        for k, spec in enumerate(specs, start=1):
            lines.append((f"{kind}[{k}]", _fmt_param(_mod_param(spec))))
            lines.append((f"{kind}[{k}]-site", spec.site))
            lines.append((f"{kind}[{k}]-position", spec.position))
    custom = [("source_format", md.source_format), ("source_file", md.source_file)]
    for n, (key, value) in enumerate([(k, v) for k, v in custom if v], start=1):
        lines.append((f"custom[{n}]", _fmt_param(("", "", key, value))))
    return [f"MTD\t{key}\t{_clean(value)}" for key, value in lines]


def _fmt_floats(col: pd.Series) -> pd.Series:
    """Shortest round-trip text; mzTab spells infinity INF / -INF."""

    def fmt(v: float) -> Optional[str]:
        if np.isnan(v):
            return None
        if np.isinf(v):
            return "INF" if v > 0 else "-INF"
        return repr(float(v))

    return col.map(fmt).astype(object)


def write_mztab(
    table: PSMTable,
    path: Union[str, PathLike, None] = None,
    ms_run_location: Optional[str] = None,
) -> str:
    """
    Serialize a PSM table as mzTab 1.0.

    Args:
        table: PSMs to write.
        path: Also write the document to this file.
        ms_run_location: Overrides ``table.metadata.ms_run_location``.

    Returns:
        The mzTab document.
    """
    md = table.metadata
    if ms_run_location is not None:
        md = SearchMetadata(**{**md.__dict__, "ms_run_location": ms_run_location})
    df = table.df

    out = pd.DataFrame(index=df.index)
    for mztab, col in _PSH:
        if col is None:
            out[mztab] = None
        else:
            out[mztab] = df[col]
    out["PSM_ID"] = np.arange(1, len(df) + 1)
    n_proteins = df["proteins"].map(len)
    out["unique"] = np.where(n_proteins == 0, None, np.where(n_proteins == 1, "1", "0"))
    out["search_engine"] = _fmt_param(_software_param(md))
    out["spectra_ref"] = "ms_run[1]:scan=" + df["scan"].astype("string")
    out[DECOY_COLUMN] = df["is_decoy"].astype(int)
    for col in df.columns[len(PSM_COLUMNS):]:
        out[OPT_PREFIX + re.sub(r"\s", "_", str(col))] = df[col]

    for col in out.columns:
        if out[col].dtype == object or pd.api.types.is_string_dtype(out[col]):
            if col != "accession":
                out[col] = out[col].map(
                    lambda v: _clean(v) if isinstance(v, str) else v
                )
        elif pd.api.types.is_float_dtype(out[col]):
            out[col] = _fmt_floats(out[col])

    # One row per protein, sharing PSM_ID; protein-less PSMs keep one row.
    out = out.explode("accession")
    out["accession"] = out["accession"].map(lambda v: _clean(v) if isinstance(v, str) else v)
    out.insert(0, "PSH", "PSM")

    body = out.to_csv(sep="\t", index=False, na_rep=NULL,
                      quoting=csv.QUOTE_NONE, lineterminator="\n")
    text = "\n".join(_metadata_lines(md)) + "\n" + body
    if path is not None:
        Path(fspath(path)).write_text(text, encoding="utf-8")
    return text


def mztab_member_name(source_name: str, compressed: bool) -> str:
    """Archive member name for search results read from ``source_name``."""
    name = source_name[:-4] if source_name.lower().endswith(".zst") else source_name
    if not name.lower().endswith(".mztab"):
        name += ".mztab"
    return name + ".zst" if compressed else name


# -- reader ----------------------------------------------------------------


def _parse_metadata(mtd: Dict[str, str]) -> Tuple[SearchMetadata, Dict[str, str]]:
    """
    Search metadata from ``MTD`` pairs.

    Returns:
        The metadata, and the core column each ``search_engine_score[n]``
        column should be read into (``score``, ``q_value``, ``pep``).
    """
    md = SearchMetadata()
    scores: Dict[int, Param] = {}
    mod_fields: Dict[Tuple[str, int], Dict[str, str]] = {}

    location = mtd.get("ms_run[1]-location")
    if location and location != NULL:
        md.ms_run_location = location[len("file://"):] if location.startswith("file://") else location

    for key, value in mtd.items():
        match = _INDEXED_KEY.match(key)
        if not match:
            continue
        name, n, sub = match.group(1), int(match.group(2)), match.group(3)
        param = _parse_param(value)
        if name == "software" and n == 1 and sub is None and param:
            if param[1] != _GENERIC_SOFTWARE[1]:
                md.software = _SOFTWARE_BY_ACCESSION.get(param[1], param[2] or None)
            md.software_version = param[3] or None
        elif name == "psm_search_engine_score" and sub is None and param:
            scores[n] = param
        elif name in ("fixed_mod", "variable_mod"):
            mod_fields.setdefault((name, n), {})[sub or "param"] = value
        elif name == "custom" and sub is None and param:
            if param[2] == "source_format":
                md.source_format = param[3] or None
            elif param[2] == "source_file":
                md.source_file = param[3] or None

    for (kind, _), fields in sorted(mod_fields.items()):
        param = _parse_param(fields.get("param", ""))
        if param is None:
            continue
        if param[1].startswith("CHEMMOD:"):
            try:
                massdiff: Optional[float] = float(param[1][len("CHEMMOD:"):])
            except ValueError:
                massdiff = None
        else:
            massdiff = mods.UNIMOD_MASSES.get(param[1])
        if massdiff is None:
            continue
        spec = ModificationSpec(massdiff, fields.get("site", ""),
                                fields.get("position", "Anywhere"))
        (md.fixed_mods if kind == "fixed_mod" else md.variable_mods).append(spec)

    roles: Dict[str, str] = {}
    for n in sorted(scores):
        _, accession, name, _ = scores[n]
        lname = name.lower()
        column = f"search_engine_score[{n}]"
        if accession in _Q_VALUE_ACCESSIONS or "q-value" in lname or "q value" in lname:
            roles.setdefault("q_value", column)
        elif accession in _PEP_ACCESSIONS or "posterior error" in lname or lname == "pep":
            roles.setdefault("pep", column)
        elif "score" not in roles:
            roles["score"] = column
            md.primary_score = _SCORE_BY_ACCESSION.get(accession, name or None)
    if not scores:
        roles["score"] = "search_engine_score[1]"
    return md, {column: role for role, column in roles.items()}


def read_mztab(source: Source) -> PSMTable:
    """
    Read the PSM section of an mzTab 1.0 file.

    Reads files written by mscompress as well as third-party mzTab: columns
    are looked up by name and missing optional columns are tolerated.

    Args:
        source: Path to the file, or its contents (zstd is decompressed).
    """
    data, _ = load(source)
    mtd: Dict[str, str] = {}
    table_lines: List[str] = []
    for line in data.decode("utf-8").splitlines():
        if line.startswith("MTD\t"):
            parts = line.split("\t")
            if len(parts) >= 3:
                mtd[parts[1]] = parts[2]
        elif line.startswith(("PSH\t", "PSM\t")):
            table_lines.append(line)

    md, score_roles = _parse_metadata(mtd)
    if not table_lines:
        return PSMTable(None, md)

    raw = pd.read_csv(io.StringIO("\n".join(table_lines)), sep="\t",
                      na_values=[NULL], keep_default_na=False,
                      quoting=csv.QUOTE_NONE, float_precision="round_trip",
                      dtype={"accession": object})
    raw = raw.drop(columns=raw.columns[0])  # the PSH/PSM line tag

    # Protein rows of one PSM share its PSM_ID.
    psm_id = raw["PSM_ID"] if "PSM_ID" in raw.columns else pd.Series(range(len(raw)))
    if "accession" in raw.columns:
        proteins = raw.groupby(psm_id.to_numpy(), sort=False)["accession"].agg(
            lambda s: [str(a) for a in s if pd.notna(a)]
        )
    else:
        proteins = None
    first = ~psm_id.duplicated().to_numpy()
    raw = raw[first].reset_index(drop=True)

    df = pd.DataFrame(index=raw.index)
    ref = raw.get("spectra_ref", pd.Series([None] * len(raw))).astype("string")
    df["scan"] = ref.str.extract(r"scan=(\d+)", expand=False)
    index = ref.str.extract(r"index=(\d+)", expand=False)
    if index.notna().any():
        df["spectrum_index"] = pd.to_numeric(index).astype("Int64")
    df["proteins"] = list(proteins) if proteins is not None else [[] for _ in range(len(raw))]
    for column, role in score_roles.items():
        if column in raw.columns:
            df[role] = raw[column]
    for mztab, col in _RENAMES.items():
        if mztab in raw.columns:
            df[col] = raw[mztab]
    if "retention_time" in df.columns and not pd.api.types.is_numeric_dtype(df["retention_time"]):
        # mzTab allows "t1|t2"; keep the first.
        df["retention_time"] = df["retention_time"].astype("string").str.split("|").str[0]
    if DECOY_COLUMN in raw.columns:
        df["is_decoy"] = raw[DECOY_COLUMN].astype("string").isin(["1", "true", "True"])

    # Anything not mapped above is kept; unmapped scores keep their name.
    standard = {m for m, _ in _PSH if not m.startswith("search_engine_score[")}
    standard |= set(score_roles)
    for col in raw.columns:
        if col in standard:
            continue
        name = col[len(OPT_PREFIX):] if col.startswith(OPT_PREFIX) else col
        df[name] = raw[col]

    return PSMTable(df, md)
