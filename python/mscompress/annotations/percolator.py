"""Percolator: its PSM output (``PSMId``) and .pin feature files (``SpecId``)."""

from __future__ import annotations

import re
from typing import List, Optional, Tuple

import pandas as pd

from mscompress.annotations import mods
from mscompress.annotations.base import SearchResultsFormat
from mscompress.annotations.metadata import SearchMetadata
from mscompress.annotations.table import PSMTable
from mscompress.types import AnnotationFormat

# "<file>.<scan>.<scan>.<charge>[_<rank>]"
_ID = re.compile(r".*?\.(\d+)\.(\d+)\.(\d+)(?:_(\d+))?$")
# "K.PEPTIDE.R": only the outer dots delimit flanks, so a mass shift such as
# M[15.9949] is left intact.
_FLANKED = re.compile(r"^([^.]?)\.(.+)\.([^.]?)$")
_BRACKET = re.compile(r"\[([^\]]*)\]")

# Columns mapped onto core PSMTable columns; the first present name is used.
_ID_COLUMNS = ("PSMId", "SpecId")
_PEPTIDE_COLUMNS = ("peptide", "Peptide")
_PROTEIN_COLUMNS = ("proteinIds", "Proteins")
_CHARGE_COLUMNS = ("Charge", "charge")
_SCORE_COLUMNS = {"score": "score", "q-value": "q_value", "posterior_error_prob": "pep"}


def _first(columns: List[str], names: Tuple[str, ...]) -> Optional[str]:
    return next((n for n in names if n in columns), None)


class Percolator(SearchResultsFormat):
    """
    Percolator PSM output or .pin feature file (tab-separated).

    Bracketed values in peptides are mass shifts (``M[15.9949]``). Percolator
    output does not record the modifications searched, so the metadata
    declares the ones observed as variable. Columns not mapped to a core
    column (features, ``ExpMass``, ...) are kept under their own names.
    """

    format = AnnotationFormat.PERCOLATOR_TSV
    extensions = (".pin", ".tsv")

    def __init__(self, decoy_prefix: str = "DECOY_"):
        """
        Args:
            decoy_prefix: Protein prefix marking decoys, used when there is
                no ``Label`` column.
        """
        self.decoy_prefix = decoy_prefix

    @classmethod
    def sniff(cls, head: str) -> bool:
        columns = head.split("\n", 1)[0].split("\t")
        return any(col in columns for col in ("PSMId", "SpecId", "ScanNr"))

    def _parse(self, data: bytes) -> PSMTable:
        md = SearchMetadata(source_format=self.format.value)
        raw = self._raw_table(data.decode("utf-8"))
        if raw is None:
            return PSMTable(None, md)
        header = list(raw.columns)
        md.software = "Percolator" if header[0] == "PSMId" else None

        df = pd.DataFrame(index=raw.index)
        used = self._ids(raw, df)
        used |= self._peptides(raw, df, md)
        used |= self._proteins(raw, df)
        for column, core in _SCORE_COLUMNS.items():
            if column in raw.columns:
                df[core] = raw[column]
                used.add(column)
        if "score" in raw.columns:
            md.primary_score = "score"

        for col in header:
            if col not in used:
                values = raw[col].replace("", None)
                numeric = pd.to_numeric(values, errors="coerce")
                df[col] = numeric if numeric.notna().sum() == values.notna().sum() else values
        return PSMTable(df, md)

    @staticmethod
    def _raw_table(text: str) -> Optional[pd.DataFrame]:
        """The file as strings. Proteins may spill into extra trailing cells."""
        lines = [l.rstrip("\r") for l in text.splitlines() if l.strip()]
        if not lines:
            return None
        header = lines[0].split("\t")
        spills = _first(header, _PROTEIN_COLUMNS) == header[-1]
        rows = []
        for line in lines[1:]:
            cells = line.split("\t")
            if cells[0].lower() == "defaultdirection":
                continue
            if spills and len(cells) > len(header):
                cells = cells[:len(header) - 1] + ["\t".join(cells[len(header) - 1:])]
            rows.append(cells + [""] * (len(header) - len(cells)))
        return pd.DataFrame(rows, columns=header, dtype=object)

    @staticmethod
    def _ids(raw: pd.DataFrame, df: pd.DataFrame) -> set:
        """``scan``, ``charge`` and ``rank``, from columns or the PSM id."""
        id_col = _first(list(raw.columns), _ID_COLUMNS)
        charge_col = _first(list(raw.columns), _CHARGE_COLUMNS)
        ids = raw[id_col].str.extract(_ID) if id_col else None
        if "ScanNr" in raw.columns:
            df["scan"] = raw["ScanNr"]
        elif ids is not None:
            df["scan"] = ids[0]
        if charge_col:
            df["charge"] = raw[charge_col]
        elif ids is not None:
            df["charge"] = ids[2]
        if ids is not None and ids[3].notna().any():
            df["rank"] = pd.to_numeric(ids[3]).astype("Int64")
        return {id_col, charge_col, "ScanNr"}

    @staticmethod
    def _peptides(raw: pd.DataFrame, df: pd.DataFrame, md: SearchMetadata) -> set:
        """Sequence, flanks and modifications from ``K.PEM[15.9949]K.R``."""
        col = _first(list(raw.columns), _PEPTIDE_COLUMNS)
        if col is None:
            return set()
        observed: List[mods.Mod] = []
        rows = []
        for value in raw[col]:
            flanked = _FLANKED.match(value or "")
            pre, core, post = flanked.groups() if flanked else ("", value or "", "")
            bare, found = Percolator._split_mods(core)
            observed.extend(found)
            rows.append((bare, core, mods.format_modifications(found), pre or None, post or None))
        parsed = pd.DataFrame(rows, index=raw.index, columns=[
            "peptide", "modified_peptide", "modifications", "pre", "post"])
        for name in parsed.columns:
            df[name] = parsed[name]
        md.variable_mods = mods.observed_mods(observed)
        return {col}

    def _proteins(self, raw: pd.DataFrame, df: pd.DataFrame) -> set:
        """``proteins`` and ``is_decoy`` (from ``Label``, else the prefix)."""
        col = _first(list(raw.columns), _PROTEIN_COLUMNS)
        if col:
            df["proteins"] = raw[col].map(
                lambda cell: [p for p in re.split(r"[\t;,]", cell) if p.strip()]
            )
        else:
            df["proteins"] = [[] for _ in range(len(raw))]
        if "Label" in raw.columns:
            df["is_decoy"] = pd.to_numeric(raw["Label"], errors="coerce") == -1
        else:
            prefix = self.decoy_prefix
            df["is_decoy"] = df["proteins"].map(
                lambda ps: bool(ps) and all(p.startswith(prefix) for p in ps)
            )
        return {col, "Label"}

    @staticmethod
    def _split_mods(modified: str) -> Tuple[str, List[mods.Mod]]:
        """Bare sequence and mods from ``n[42.0106]PEM[15.9949]K`` notation."""
        bare = re.sub(r"^n(?=[A-Z])|(?<=[A-Z])c$", "", _BRACKET.sub("", modified))
        found: List[mods.Mod] = []
        pos, site = 0, "N-term"
        i = 0
        while i < len(modified):
            ch = modified[i]
            if ch == "[":
                end = modified.find("]", i)
                if end < 0:
                    break
                try:
                    found.append((pos, float(modified[i + 1:end]), site))
                except ValueError:
                    pass
                i = end + 1
                continue
            if ch.isupper():
                pos, site = pos + 1, ch
            elif ch == "n":
                pos, site = 0, "N-term"
            elif ch == "c":
                pos, site = sum(aa.isupper() for aa in bare) + 1, "C-term"
            i += 1
        return bare, found
