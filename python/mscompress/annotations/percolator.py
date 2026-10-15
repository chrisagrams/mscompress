"""Percolator: its PSM output (``PSMId``) and .pin feature files (``SpecId``)."""

from __future__ import annotations

import io
import re
from typing import List, Tuple

import pandas as pd

from mscompress.annotations import mods
from mscompress.annotations.base import SearchResultsFormat
from mscompress.annotations.metadata import SearchMetadata
from mscompress.annotations.table import PSMTable
from mscompress.types import AnnotationFormat

# Percolator output and .pin files name some columns differently; both map
# onto PSMTable columns. Other columns (features, ExpMass, ...) keep their name.
_RENAMES = {
    "ScanNr": "scan",
    "Charge": "charge",
    "Peptide": "peptide",
    "Proteins": "proteins",
    "proteinIds": "proteins",
    "q-value": "q_value",
    "posterior_error_prob": "pep",
}
# "<file>.<scan>.<scan>.<charge>[_<rank>]"
_ID = re.compile(r"\.(\d+)\.\d+\.(\d+)(?:_(\d+))?$")
# "K.PEPTIDE.R": only the outer dots delimit flanks, so a mass shift such as
# M[15.9949] is left intact.
_FLANKED = re.compile(r"^(.?)\.(.+)\.(.?)$")


class Percolator(SearchResultsFormat):
    """
    Percolator PSM output or .pin feature file (tab-separated).

    Bracketed values in peptides are mass shifts (``M[15.9949]``). Percolator
    output does not record the modifications searched, so the metadata
    declares the ones observed as variable.
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
        return head.startswith(("PSMId\t", "SpecId\t"))

    def _parse(self, data: bytes) -> PSMTable:
        lines = data.decode("utf-8").splitlines()
        header = lines[0].split("\t")
        # Proteins may spill into extra trailing cells; join them with ";".
        n = len(header)
        rows = [
            "\t".join(cells[:n - 1] + [";".join(cells[n - 1:])])
            for cells in (line.split("\t") for line in lines[1:] if line)
            if cells[0] != "DefaultDirection"
        ]
        df = pd.read_csv(io.StringIO("\n".join([lines[0], *rows])), sep="\t")
        df = df.rename(columns=_RENAMES)

        percolator = header[0] == "PSMId"
        md = SearchMetadata(
            software="Percolator" if percolator else None,
            primary_score=("percolator:score" if percolator else "score")
            if "score" in df else None,
            source_format=self.format.value,
        )

        # Percolator output has no scan or charge column: take them from the id.
        ids = df[header[0]].str.extract(_ID)
        if "scan" not in df:
            df["scan"] = ids[0]
        if "charge" not in df:
            df["charge"] = ids[1]
        if ids[2].notna().any():
            df["rank"] = pd.to_numeric(ids[2])

        parsed = pd.DataFrame([self._split_peptide(p) for p in df["peptide"]], index=df.index,
                              columns=["pre", "peptide", "modified_peptide", "post", "mods"])
        df[["pre", "peptide", "modified_peptide", "post"]] = parsed.iloc[:, :4]
        df["modifications"] = parsed["mods"].map(mods.format_modifications)
        md.variable_mods = mods.observed_mods(m for found in parsed["mods"] for m in found)

        df["proteins"] = df["proteins"].str.strip(";").str.split(";")
        if "Label" in df:
            df["is_decoy"] = df["Label"] == -1
        else:
            df["is_decoy"] = [all(p.startswith(self.decoy_prefix) for p in ps)
                              for ps in df["proteins"]]
        # The id and label are now in scan / charge / rank / is_decoy.
        return PSMTable(df.drop(columns=[header[0], "Label"], errors="ignore"), md)

    @staticmethod
    def _split_peptide(value: str) -> Tuple[str, str, str, str, List[mods.Mod]]:
        """(pre, bare sequence, modified sequence, post, mods) from ``K.n[42.0106]PEM[15.9949]K.R``."""
        pre, modified, post = _FLANKED.match(value).groups()
        found: List[mods.Mod] = []
        bare = ""
        site = "N-term"
        for token in re.findall(r"\[[^\]]*\]|[A-Za-z]", modified):
            if token.startswith("["):
                pos = len(bare) + (1 if site == "C-term" else 0)
                found.append((pos, float(token[1:-1]), site))
            elif token == "n":
                site = "N-term"
            elif token == "c":
                site = "C-term"
            else:
                bare += token
                site = token
        return pre or None, bare, modified, post or None, found
