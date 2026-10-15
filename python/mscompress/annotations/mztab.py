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
from typing import Dict, List, NamedTuple, Optional, Tuple, Union

import numpy as np
import pandas as pd

from mscompress.annotations import mods
from mscompress.annotations.base import SearchResultsFormat
from mscompress.annotations.metadata import ModificationSpec, SearchMetadata
from mscompress.annotations.table import PSM_COLUMNS, PSMTable, psm_frame
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

class Param(NamedTuple):
    """A controlled-vocabulary parameter, written ``[cv, accession, name, value]``.

    User parameters leave ``cv`` and ``accession`` empty.
    """

    cv: str
    accession: str
    name: str
    value: str = ""

    def __str__(self) -> str:
        return "[" + ", ".join(self) + "]"

    @classmethod
    def parse(cls, text: str) -> Optional[Param]:
        text = text.strip()
        if not (text.startswith("[") and text.endswith("]")):
            return None
        parts = [p.strip() for p in text[1:-1].split(",", 3)]
        return cls(*parts) if len(parts) == 4 else None


class CV:
    """The PSI-MS and UNIMOD terms used to describe a search in mzTab."""

    # Search engines with their own PSI-MS term, keyed by _key(name).
    SOFTWARE: Dict[str, Tuple[str, str]] = {
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
    GENERIC_SOFTWARE = "MS:1001456"

    # Primary score name (lowercase) -> parameter.
    SCORES: Dict[str, Param] = {
        "hyperscore": Param("MS", "MS:1001331", "X!Tandem:hyperscore"),
        "expect": Param("MS", "MS:1001330", "X!Tandem:expect"),
        "xcorr": Param("MS", "MS:1001155", "SEQUEST:xcorr"),
        "percolator:score": Param("MS", "MS:1001492", "percolator:score"),
    }
    GENERIC_SCORE = Param("MS", "MS:1001153", "search engine specific score")
    Q_VALUE = Param("MS", "MS:1002354", "PSM-level q-value")
    PEP = Param("", "", "posterior error probability")

    NO_FIXED_MODS = Param("MS", "MS:1002453", "No fixed modifications searched")
    NO_VARIABLE_MODS = Param("MS", "MS:1002454", "No variable modifications searched")

    @staticmethod
    def _key(name: str) -> str:
        return re.sub(r"[^a-z0-9]", "", name.lower())

    @classmethod
    def software(cls, md: SearchMetadata) -> Param:
        """Software as its PSI-MS term, with the version in the value slot."""
        version = md.software_version or ""
        if not md.software:
            return Param("MS", cls.GENERIC_SOFTWARE, "analysis software", version)
        term = cls.SOFTWARE.get(cls._key(md.software))
        if term is None:
            return Param("", "", md.software, version)
        return Param("MS", term[0], term[1], version)

    @classmethod
    def read_software(cls, param: Param) -> Tuple[Optional[str], Optional[str]]:
        """(software, version) from a ``software[1]`` parameter."""
        names = {acc: name for acc, name in cls.SOFTWARE.values()}
        software = None if param.accession == cls.GENERIC_SOFTWARE else (
            names.get(param.accession, param.name or None))
        return software, param.value or None

    @classmethod
    def scores(cls, md: SearchMetadata) -> List[Param]:
        """Parameters for the primary score, q-value and PEP, in that order."""
        name = md.primary_score
        if not name:
            primary = cls.GENERIC_SCORE
        else:
            primary = cls.SCORES.get(name.lower(), Param("", "", name))
        return [primary, cls.Q_VALUE, cls.PEP]

    @classmethod
    def score_name(cls, param: Param) -> Optional[str]:
        """Primary score name for a score parameter."""
        if param.accession == cls.GENERIC_SCORE.accession:
            return None
        names = {p.accession: name for name, p in cls.SCORES.items()}
        return names.get(param.accession, param.name or None)

    @staticmethod
    def modification(spec: ModificationSpec) -> Param:
        """UNIMOD parameter for a modification, or CHEMMOD when there is none."""
        hit = mods.unimod(spec.massdiff)
        if hit:
            return Param("UNIMOD", f"UNIMOD:{hit[0]}", hit[1])
        accession = mods.chemmod(spec.massdiff)
        # The name repeats the accession: jmzTab rejects an empty name.
        return Param("CHEMMOD", accession, accession)

    @staticmethod
    def modification_mass(param: Param) -> Optional[float]:
        """Mass shift of a UNIMOD / CHEMMOD parameter, if known."""
        if param.accession.startswith("CHEMMOD:"):
            try:
                return float(param.accession[len("CHEMMOD:"):])
            except ValueError:
                return None
        return mods.UNIMOD_MASSES.get(param.accession)


class MetadataSection:
    """
    The ``MTD`` section: :class:`SearchMetadata` as mzTab key/value lines.

    Only the layout :meth:`lines` writes is read back: the score columns are
    always the primary score, q-value and PEP, in that order.
    """

    @staticmethod
    def lines(md: SearchMetadata) -> List[str]:
        """The ``MTD`` lines for a search."""
        if md.source_format:
            description = f"Converted by mscompress from {md.source_format}"
            if md.source_file:
                description += f" ({md.source_file})"
        else:
            description = "PSMs written by mscompress"
        pairs: List[Tuple[str, object]] = [
            ("mzTab-version", "1.0.0"),
            ("mzTab-mode", "Summary"),
            ("mzTab-type", "Identification"),
            ("description", description),
            ("ms_run[1]-location",
             f"file://{md.ms_run_location}" if md.ms_run_location else NULL),
            ("software[1]", CV.software(md)),
        ]
        for n, param in enumerate(CV.scores(md), start=1):
            pairs.append((f"psm_search_engine_score[{n}]", param))
        for kind, specs, none in (("fixed_mod", md.fixed_mods, CV.NO_FIXED_MODS),
                                  ("variable_mod", md.variable_mods, CV.NO_VARIABLE_MODS)):
            if not specs:
                pairs.append((f"{kind}[1]", none))
            for n, spec in enumerate(specs, start=1):
                pairs += [(f"{kind}[{n}]", CV.modification(spec)),
                          (f"{kind}[{n}]-site", spec.site),
                          (f"{kind}[{n}]-position", spec.position)]
        custom = [(k, v) for k, v in (("source_format", md.source_format),
                                      ("source_file", md.source_file)) if v]
        for n, (key, value) in enumerate(custom, start=1):
            pairs.append((f"custom[{n}]", Param("", "", key, value)))
        return [f"MTD\t{key}\t{value}" for key, value in pairs]

    @staticmethod
    def parse(pairs: Dict[str, str]) -> SearchMetadata:
        """Search metadata from ``MTD`` key/value pairs."""
        md = SearchMetadata()
        location = pairs.get("ms_run[1]-location", NULL)
        if location != NULL:
            md.ms_run_location = location.removeprefix("file://")
        software = Param.parse(pairs.get("software[1]", ""))
        if software:
            md.software, md.software_version = CV.read_software(software)
        score = Param.parse(pairs.get("psm_search_engine_score[1]", ""))
        if score:
            md.primary_score = CV.score_name(score)

        for kind, specs in (("fixed_mod", md.fixed_mods), ("variable_mod", md.variable_mods)):
            n = 1
            while f"{kind}[{n}]" in pairs:
                key = f"{kind}[{n}]"
                param = Param.parse(pairs[key])
                mass = CV.modification_mass(param) if param else None
                if mass is not None:  # None: "No ... modifications searched"
                    specs.append(ModificationSpec(mass, pairs.get(f"{key}-site", ""),
                                                  pairs.get(f"{key}-position", "Anywhere")))
                n += 1

        custom = {}
        n = 1
        while f"custom[{n}]" in pairs:
            if param := Param.parse(pairs[f"custom[{n}]"]):
                custom[param.name] = param.value or None
            n += 1
        md.source_format = custom.get("source_format")
        md.source_file = custom.get("source_file")
        return md


class MzTab(SearchResultsFormat):
    """
    mzTab 1.0 (PSM section): read, and written for MSZX storage.

    Reads the layout :meth:`write` produces (what MSZX archives contain);
    mzTab from other tools is not supported.
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
        text = "\n".join(MetadataSection.lines(md)) + "\n" + body
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
        out["search_engine"] = str(CV.software(md))
        out["spectra_ref"] = "ms_run[1]:scan=" + df["scan"].astype("string")
        out[DECOY_COLUMN] = df["is_decoy"].astype(int)
        for col in df.columns[len(PSM_COLUMNS):]:
            out[OPT_PREFIX + re.sub(r"\s", "_", str(col))] = df[col]

        # mzTab spells infinity INF / -INF. (Two scalar calls: pandas 3's list
        # form raises on this frame.)
        out = out.replace(np.inf, "INF").replace(-np.inf, "-INF")

        # One row per protein, sharing PSM_ID; protein-less PSMs keep one row.
        out = out.explode("accession")
        out.insert(0, "PSH", "PSM")
        return out

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

        md = MetadataSection.parse(mtd)
        if len(table_lines) < 2:  # no PSM rows
            return PSMTable(None, md)
        raw = pd.read_csv(io.StringIO("\n".join(table_lines)), sep="\t",
                          na_values=[NULL], keep_default_na=False,
                          quoting=csv.QUOTE_NONE, float_precision="round_trip",
                          dtype={"accession": object})
        return PSMTable(psm_frame(self._parse_psms(raw.iloc[:, 1:])), md)

    @staticmethod
    def _parse_psms(raw: pd.DataFrame) -> pd.DataFrame:
        """PSMTable columns from the PSH/PSM table (without the line tag)."""
        # Protein rows of one PSM share its PSM_ID.
        proteins = raw.groupby("PSM_ID", sort=False)["accession"].agg(
            lambda s: [str(a) for a in s if pd.notna(a)]
        )
        raw = raw.drop_duplicates("PSM_ID").reset_index(drop=True)

        df = pd.DataFrame({col: raw[mztab] for mztab, col in _PSH if col})
        df["scan"] = pd.to_numeric(raw["spectra_ref"].str.extract(r"scan=(\d+)", expand=False))
        df["proteins"] = list(proteins)
        for col in raw.columns:
            if col.startswith(OPT_PREFIX) and col != DECOY_COLUMN:
                df[col.removeprefix(OPT_PREFIX)] = raw[col]
        return df
