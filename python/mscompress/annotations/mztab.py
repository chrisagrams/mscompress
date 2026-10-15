"""
mzTab 1.0: the format MSZX archives store search results in.

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
from dataclasses import replace
from os import PathLike, fspath
from pathlib import Path
from typing import Dict, List, Optional, Tuple, Union

import numpy as np
import pandas as pd

from mscompress.annotations import _cv
from mscompress.annotations.base import SearchResultsFormat
from mscompress.annotations.metadata import ModificationSpec, SearchMetadata
from mscompress.annotations.table import PSM_COLUMNS, PSMTable
from mscompress.types import AnnotationFormat

NULL = "null"
OPT_PREFIX = "opt_global_"
DECOY_COLUMN = "opt_global_cv_MS:1002217_decoy_peptide"

# mzTab PSM columns in the order written, and the PSMTable column each is
# filled from (None: derived while writing, or left null).
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
# Columns that read straight back into a PSMTable column. The rest (scores,
# spectra_ref, accession, decoy flag) need converting.
_COPIED = {
    mztab: col for mztab, col in _PSH
    if col in ("peptide", "modifications", "retention_time", "charge",
               "precursor_mz", "calc_mz", "pre", "post")
}

_INDEXED_KEY = re.compile(r"^([A-Za-z_]+)\[(\d+)\](?:-(.+))?$")


def _format_floats(col: pd.Series) -> pd.Series:
    """Shortest round-trip text; mzTab spells infinity INF / -INF."""

    def fmt(v: float) -> Optional[str]:
        if np.isnan(v):
            return None
        if np.isinf(v):
            return "INF" if v > 0 else "-INF"
        return repr(float(v))

    return col.map(fmt).astype(object)


Pair = Tuple[str, str]


class MetadataSection:
    """
    The ``MTD`` section: :class:`SearchMetadata` as mzTab key/value lines.

    ``lines()`` writes it and ``parse()`` reads it back. Reading also works
    out which ``search_engine_score[n]`` column holds the score, q-value and
    PEP, since third-party files order them differently.
    """

    #: The score columns as written: mzTab column -> PSMTable column.
    WRITTEN_SCORES = {
        "search_engine_score[1]": "score",
        "search_engine_score[2]": "q_value",
        "search_engine_score[3]": "pep",
    }

    def __init__(self, metadata: SearchMetadata,
                 score_columns: Optional[Dict[str, str]] = None):
        self.metadata = metadata
        #: mzTab score column -> PSMTable column (``score``, ``q_value``, ``pep``).
        self.score_columns = dict(self.WRITTEN_SCORES if score_columns is None else score_columns)

    # -- writing -----------------------------------------------------------

    def lines(self) -> List[str]:
        """The ``MTD`` lines, in the order mzTab lists them."""
        pairs = [
            *self._header(),
            *self._software(),
            *self._scores(),
            *self._mods("fixed_mod", self.metadata.fixed_mods, _cv.NO_FIXED_MODS),
            *self._mods("variable_mod", self.metadata.variable_mods, _cv.NO_VARIABLE_MODS),
            *self._custom(),
        ]
        return [f"MTD\t{key}\t{_cv.clean(value)}" for key, value in pairs]

    def _header(self) -> List[Pair]:
        md = self.metadata
        if md.source_format:
            description = f"Converted by mscompress from {md.source_format}"
            if md.source_file:
                description += f" ({md.source_file})"
        else:
            description = "PSMs written by mscompress"
        location = f"file://{md.ms_run_location}" if md.ms_run_location else NULL
        return [
            ("mzTab-version", "1.0.0"),
            ("mzTab-mode", "Summary"),
            ("mzTab-type", "Identification"),
            ("description", description),
            ("ms_run[1]-location", location),
        ]

    def _software(self) -> List[Pair]:
        return [("software[1]", _cv.format_param(_cv.software_param(self.metadata)))]

    def _scores(self) -> List[Pair]:
        return [(f"psm_search_engine_score[{n}]", _cv.format_param(param))
                for n, param in enumerate(_cv.score_params(self.metadata), start=1)]

    @staticmethod
    def _mods(kind: str, specs: List[ModificationSpec], none: _cv.Param) -> List[Pair]:
        if not specs:
            return [(f"{kind}[1]", _cv.format_param(none))]
        pairs = []
        for k, spec in enumerate(specs, start=1):
            pairs += [
                (f"{kind}[{k}]", _cv.format_param(_cv.mod_param(spec))),
                (f"{kind}[{k}]-site", spec.site),
                (f"{kind}[{k}]-position", spec.position),
            ]
        return pairs

    def _custom(self) -> List[Pair]:
        md = self.metadata
        values = [(k, v) for k, v in (("source_format", md.source_format),
                                      ("source_file", md.source_file)) if v]
        return [(f"custom[{n}]", _cv.format_param(("", "", key, value)))
                for n, (key, value) in enumerate(values, start=1)]

    # -- reading -----------------------------------------------------------

    @classmethod
    def parse(cls, pairs: Dict[str, str]) -> MetadataSection:
        """Read ``MTD`` key/value pairs."""
        section = cls(SearchMetadata(), score_columns={})
        md = section.metadata
        location = pairs.get("ms_run[1]-location")
        if location and location != NULL:
            md.ms_run_location = location.removeprefix("file://")

        mod_fields: Dict[Tuple[str, int], Dict[str, str]] = {}
        for key, value in pairs.items():
            match = _INDEXED_KEY.match(key)
            if not match:
                continue
            name, n, sub = match.group(1), int(match.group(2)), match.group(3)
            if name in ("fixed_mod", "variable_mod"):
                mod_fields.setdefault((name, n), {})[sub or "param"] = value
                continue
            param = _cv.parse_param(value)
            if param is None or sub is not None:
                continue
            if name == "software" and n == 1:
                md.software, md.software_version = _cv.software_from_param(param)
            elif name == "psm_search_engine_score":
                section._read_score(n, param)
            elif name == "custom" and param[2] in ("source_format", "source_file"):
                setattr(md, param[2], param[3] or None)

        if not section.score_columns:
            section.score_columns["search_engine_score[1]"] = "score"
        for (kind, _), fields in sorted(mod_fields.items()):
            section._read_mod(kind, fields)
        return section

    def _read_score(self, n: int, param: _cv.Param) -> None:
        role = _cv.score_role(param)
        if role in self.score_columns.values():
            return
        self.score_columns[f"search_engine_score[{n}]"] = role
        if role == "score":
            self.metadata.primary_score = _cv.score_name(param)

    def _read_mod(self, kind: str, fields: Dict[str, str]) -> None:
        param = _cv.parse_param(fields.get("param", ""))
        mass = _cv.mod_mass(param) if param else None
        if mass is None:
            return
        spec = ModificationSpec(mass, fields.get("site", ""), fields.get("position", "Anywhere"))
        md = self.metadata
        (md.fixed_mods if kind == "fixed_mod" else md.variable_mods).append(spec)


class MzTab(SearchResultsFormat):
    """
    mzTab 1.0 (PSM section): read, and written for MSZX storage.

    Reads files written by mscompress as well as third-party mzTab: columns
    are looked up by name and missing optional columns are tolerated.
    """

    format = AnnotationFormat.MZTAB
    extensions = (".mztab",)

    @classmethod
    def sniff(cls, head: str) -> bool:
        return head.startswith(("MTD\t", "COM\t")) or "\nPSH\t" in head

    @staticmethod
    def member_name(source_name: str, compressed: bool) -> str:
        """Archive member name for search results read from ``source_name``."""
        name = source_name[:-4] if source_name.lower().endswith(".zst") else source_name
        if not name.lower().endswith(".mztab"):
            name += ".mztab"
        return name + ".zst" if compressed else name

    # -- writing -----------------------------------------------------------

    def write(
        self,
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
            md = replace(md, ms_run_location=ms_run_location)
        psms = self._psm_frame(table.df, md)
        body = psms.to_csv(sep="\t", index=False, na_rep=NULL,
                           quoting=csv.QUOTE_NONE, lineterminator="\n")
        text = "\n".join(MetadataSection(md).lines()) + "\n" + body
        if path is not None:
            Path(fspath(path)).write_text(text, encoding="utf-8")
        return text

    @staticmethod
    def _psm_frame(df: pd.DataFrame, md: SearchMetadata) -> pd.DataFrame:
        """The PSH/PSM lines as a DataFrame: one row per (PSM, protein)."""
        out = pd.DataFrame({mztab: df[col] if col else None for mztab, col in _PSH},
                           index=df.index)
        out["PSM_ID"] = np.arange(1, len(df) + 1)
        n_proteins = df["proteins"].map(len)
        out["unique"] = np.where(n_proteins == 0, None, np.where(n_proteins == 1, "1", "0"))
        out["search_engine"] = _cv.format_param(_cv.software_param(md))
        out["spectra_ref"] = "ms_run[1]:scan=" + df["scan"].astype("string")
        out[DECOY_COLUMN] = df["is_decoy"].astype(int)
        for col in df.columns[len(PSM_COLUMNS):]:
            out[OPT_PREFIX + re.sub(r"\s", "_", str(col))] = df[col]

        for col in out.columns:
            if pd.api.types.is_float_dtype(out[col]):
                out[col] = _format_floats(out[col])

        # One row per protein, sharing PSM_ID; protein-less PSMs keep one row.
        out = out.explode("accession")
        out.insert(0, "PSH", "PSM")
        return out

    # -- reading -----------------------------------------------------------

    def _parse(self, data: bytes) -> PSMTable:
        mtd: Dict[str, str] = {}
        table_lines: List[str] = []
        for line in data.decode("utf-8").splitlines():
            if line.startswith("MTD\t"):
                parts = line.split("\t")
                if len(parts) >= 3:
                    mtd[parts[1]] = parts[2]
            elif line.startswith(("PSH\t", "PSM\t")):
                table_lines.append(line)

        section = MetadataSection.parse(mtd)
        md = section.metadata
        if not table_lines:
            return PSMTable(None, md)
        raw = pd.read_csv(io.StringIO("\n".join(table_lines)), sep="\t",
                          na_values=[NULL], keep_default_na=False,
                          quoting=csv.QUOTE_NONE, float_precision="round_trip",
                          dtype={"accession": object})
        return PSMTable(self._parse_psms(raw.iloc[:, 1:], section.score_columns), md)

    @staticmethod
    def _parse_psms(raw: pd.DataFrame, score_columns: Dict[str, str]) -> pd.DataFrame:
        """PSMTable columns from the PSH/PSM table (without the line tag)."""
        # Protein rows of one PSM share its PSM_ID.
        psm_id = raw["PSM_ID"] if "PSM_ID" in raw.columns else pd.Series(range(len(raw)))
        if "accession" in raw.columns:
            proteins = list(raw.groupby(psm_id.to_numpy(), sort=False)["accession"].agg(
                lambda s: [str(a) for a in s if pd.notna(a)]
            ))
        else:
            proteins = None
        raw = raw[~psm_id.duplicated().to_numpy()].reset_index(drop=True)

        df = pd.DataFrame(index=raw.index)
        ref = raw.get("spectra_ref", pd.Series([None] * len(raw))).astype("string")
        df["scan"] = ref.str.extract(r"scan=(\d+)", expand=False)
        index = ref.str.extract(r"index=(\d+)", expand=False)
        if index.notna().any():
            df["spectrum_index"] = pd.to_numeric(index).astype("Int64")
        df["proteins"] = proteins if proteins is not None else [[] for _ in range(len(raw))]
        for column, role in score_columns.items():
            if column in raw.columns:
                df[role] = raw[column]
        for mztab, col in _COPIED.items():
            if mztab in raw.columns:
                df[col] = raw[mztab]
        if "retention_time" in df.columns and not pd.api.types.is_numeric_dtype(df["retention_time"]):
            # mzTab allows "t1|t2"; keep the first.
            df["retention_time"] = df["retention_time"].astype("string").str.split("|").str[0]
        if DECOY_COLUMN in raw.columns:
            df["is_decoy"] = raw[DECOY_COLUMN].astype("string").isin(["1", "true", "True"])

        # Anything not mapped above is kept; unmapped scores keep their name.
        mapped = {m for m, _ in _PSH if not m.startswith("search_engine_score[")}
        mapped |= set(score_columns)
        for col in raw.columns:
            if col not in mapped:
                df[col.removeprefix(OPT_PREFIX)] = raw[col]
        return df
