"""Percolator reader: Percolator's PSM output (``PSMId``) and .pin feature files."""

from __future__ import annotations

import re
from typing import Any, Dict, List, Optional, Tuple

import pandas as pd

from mscompress.annotations import mods
from mscompress.annotations._io import Source, load
from mscompress.annotations.metadata import SearchMetadata
from mscompress.annotations.table import PSMTable

# "<file>.<scan>.<scan>.<charge>[_<rank>]"
_ID = re.compile(r".*?\.(\d+)\.(\d+)\.(\d+)(?:_(\d+))?$")
# "K.PEPTIDE.R": only the outer dots delimit flanks, so a mass shift such as
# M[15.9949] is left intact.
_FLANKED = re.compile(r"^([^.]?)\.(.+)\.([^.]?)$")
_BRACKET = re.compile(r"\[([^\]]*)\]")

_ID_COLUMNS = ("PSMId", "SpecId")
_PEPTIDE_COLUMNS = ("peptide", "Peptide")
_PROTEIN_COLUMNS = ("proteinIds", "Proteins")
_CHARGE_COLUMNS = ("Charge", "charge")


def _first(columns, names) -> Optional[str]:
    return next((n for n in names if n in columns), None)


def _peptide_mods(modified: str) -> Tuple[str, List[mods.Mod]]:
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


def _split_proteins(cell: str) -> List[str]:
    return [p for p in re.split(r"[\t;,]", cell) if p.strip()]


def read_percolator(source: Source, decoy_prefix: str = "DECOY_") -> PSMTable:
    """
    Read Percolator PSM output or a .pin feature file (tab-separated).

    Bracketed values in peptides are mass shifts (``M[15.9949]``). Percolator
    output does not record the modifications searched, so the metadata
    declares the ones observed as variable.

    Args:
        source: Path to the file, or its contents (zstd is decompressed).
        decoy_prefix: Protein prefix marking decoys, used when there is no
            ``Label`` column.

    Returns:
        The PSMs. Feature and score columns not mapped to a core column are
        kept under their own names.
    """
    data, name = load(source)
    lines = [l for l in data.decode("utf-8").splitlines() if l.strip()]
    md = SearchMetadata(source_format="percolator_tsv", source_file=name)
    if not lines:
        return PSMTable(None, md)

    header = lines[0].rstrip("\r").split("\t")
    protein_col = _first(header, _PROTEIN_COLUMNS)
    rows = []
    for line in lines[1:]:
        cells = line.rstrip("\r").split("\t")
        # Proteins may spill over into extra trailing cells.
        if protein_col == header[-1] and len(cells) > len(header):
            cells = cells[:len(header) - 1] + ["\t".join(cells[len(header) - 1:])]
        if len(cells) > 1 and cells[0].lower() == "defaultdirection":
            continue
        rows.append(cells + [""] * (len(header) - len(cells)))
    raw = pd.DataFrame(rows, columns=header, dtype=object)

    md.software = "Percolator" if header[0] == "PSMId" else None
    df = pd.DataFrame(index=raw.index)

    id_col = _first(header, _ID_COLUMNS)
    ids = raw[id_col].str.extract(_ID) if id_col else None
    if "ScanNr" in raw.columns:
        df["scan"] = raw["ScanNr"]
    elif ids is not None:
        df["scan"] = ids[0]
    charge_col = _first(header, _CHARGE_COLUMNS)
    if charge_col:
        df["charge"] = raw[charge_col]
    elif ids is not None:
        df["charge"] = ids[2]
    if ids is not None and ids[3].notna().any():
        df["rank"] = pd.to_numeric(ids[3]).astype("Int64")

    peptide_col = _first(header, _PEPTIDE_COLUMNS)
    all_mods: List[mods.Mod] = []
    if peptide_col:
        parsed: List[Dict[str, Any]] = []
        for value in raw[peptide_col]:
            flanked = _FLANKED.match(value or "")
            pre, core, post = flanked.groups() if flanked else (None, value or "", None)
            bare, found = _peptide_mods(core)
            all_mods.extend(found)
            parsed.append({
                "peptide": bare,
                "modified_peptide": core,
                "modifications": mods.format_modifications(found),
                "pre": pre or None,
                "post": post or None,
            })
        df = df.join(pd.DataFrame(parsed, index=raw.index))
    md.variable_mods = mods.observed_mods(all_mods)

    proteins = raw[protein_col].map(_split_proteins) if protein_col else None
    df["proteins"] = proteins if proteins is not None else [[] for _ in range(len(raw))]
    if "Label" in raw.columns:
        df["is_decoy"] = pd.to_numeric(raw["Label"], errors="coerce") == -1
    else:
        df["is_decoy"] = df["proteins"].map(
            lambda ps: bool(ps) and all(p.startswith(decoy_prefix) for p in ps)
        )

    if "score" in raw.columns:
        df["score"] = raw["score"]
        md.primary_score = "score"
    if "q-value" in raw.columns:
        df["q_value"] = raw["q-value"]
    if "posterior_error_prob" in raw.columns:
        df["pep"] = raw["posterior_error_prob"]

    used = {id_col, peptide_col, protein_col, charge_col, "ScanNr", "Label",
            "score", "q-value", "posterior_error_prob"}
    for col in header:
        if col not in used:
            values = raw[col].replace("", None)
            numeric = pd.to_numeric(values, errors="coerce")
            df[col] = numeric if numeric.notna().sum() == values.notna().sum() else values
    return PSMTable(df, md)
