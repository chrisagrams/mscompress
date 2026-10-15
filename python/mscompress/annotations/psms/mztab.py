"""
mzTab reader and writer.

mzTab 1.0 (Summary mode, Identification type, PSM section) is the single
format search results are stored in inside an MSZX archive. Engine output
(pepXML, Percolator TSV, ...) is converted with :func:`write_mztab` when the
archive is built, and read back with :class:`MzTabReader`.

The layout written here is shared with the Node.js binding, which writes and
reads the same files.
"""

from __future__ import annotations

import math
import re
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Set, Tuple, Union

from mscompress.annotations._base import BaseAnnotationFile
from mscompress.annotations.psms._base import BasePSMReader
from mscompress.annotations.psms._types import PSM, ModificationSpec, SearchInfo
from mscompress.types import AnnotationFormat

NULL = "null"

# A CV/user parameter: (cv label, accession, name, value).
Param = Tuple[str, str, str, str]

DECOY_COLUMN = "opt_global_cv_MS:1002217_decoy_peptide"
EXTRA_PREFIX = "opt_global_extra_"

PSH_COLUMNS = [
    "sequence",
    "PSM_ID",
    "accession",
    "unique",
    "database",
    "database_version",
    "search_engine",
    "search_engine_score[1]",
    "search_engine_score[2]",
    "search_engine_score[3]",
    "modifications",
    "retention_time",
    "charge",
    "exp_mass_to_charge",
    "calc_mass_to_charge",
    "spectra_ref",
    "pre",
    "post",
    "start",
    "end",
    DECOY_COLUMN,
    "opt_global_modified_sequence",
    "opt_global_rank",
    "opt_global_scan_number",
    "opt_global_spectrum_index",
    "opt_global_precursor_mass",
    "opt_global_mass_diff",
    "opt_global_num_matched_ions",
    "opt_global_num_total_ions",
]

# Primary score name (lowercase) -> CV param. "score" maps only for Percolator.
_PRIMARY_SCORE_PARAMS: Dict[str, Param] = {
    "hyperscore": ("MS", "MS:1001331", "X!Tandem:hyperscore", ""),
    "expect": ("MS", "MS:1001330", "X!Tandem:expect", ""),
    "xcorr": ("MS", "MS:1001155", "SEQUEST:xcorr", ""),
}
_GENERIC_SCORE_PARAM: Param = ("MS", "MS:1001153", "search engine specific score", "")
# Reverse of the above, so a read-back SearchInfo matches what was written.
_SCORE_NAMES_BY_ACCESSION: Dict[str, Optional[str]] = {
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

_SCAN_RE = re.compile(r"scan=(\d+)")
_INDEX_RE = re.compile(r"index=(\d+)")
_INDEXED_KEY_RE = re.compile(r"^([A-Za-z_]+)\[(\d+)\](?:-(.+))?$")


# -- formatting ------------------------------------------------------------


def _clean(value: str) -> str:
    """Replace characters that would break a TSV cell."""
    return value.replace("\t", " ").replace("\r", " ").replace("\n", " ")


def _fmt_float(value: Optional[float]) -> str:
    if value is None:
        return NULL
    value = float(value)
    if math.isnan(value):
        return "NaN"
    if math.isinf(value):
        return "INF" if value > 0 else "-INF"
    return repr(value)


def _fmt_int(value: Optional[int]) -> str:
    return NULL if value is None else str(int(value))


def _fmt_extra(value: Any) -> str:
    if value is None:
        return NULL
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return _fmt_float(float(value))
    return _clean(str(value))


def _fmt_param(param: Param) -> str:
    return "[" + ", ".join(_clean(part) for part in param) + "]"


def _fmt_massdiff(massdiff: float) -> str:
    return ("-" if massdiff < 0 else "+") + _fmt_float(abs(massdiff))


def _is_percolator(info: SearchInfo) -> bool:
    return (info.software or "").lower() == "percolator"


def _software_param(info: SearchInfo) -> Param:
    if _is_percolator(info):
        return ("MS", "MS:1001490", "Percolator", "")
    return ("MS", "MS:1001456", "analysis software", info.software or "")


def _score_params(info: SearchInfo) -> List[Param]:
    percolator = _is_percolator(info)
    name = info.primary_score_name
    if not name:
        primary = _GENERIC_SCORE_PARAM
    elif name.lower() == "score" and percolator:
        primary = ("MS", "MS:1001492", "percolator:score", "")
    else:
        primary = _PRIMARY_SCORE_PARAMS.get(name.lower(), ("", "", name, ""))

    if percolator:
        q_value: Param = ("MS", "MS:1001491", "percolator:Q value", "")
        pep: Param = ("MS", "MS:1001493", "percolator:PEP", "")
    else:
        q_value = ("MS", "MS:1002354", "PSM-level q-value", "")
        pep = ("", "", "posterior error probability", "")
    return [primary, q_value, pep]


# -- writer ----------------------------------------------------------------


def write_mztab(
    psms: Iterable[PSM],
    search_info: Optional[SearchInfo] = None,
    ms_run_location: Optional[str] = None,
    source_format: Optional[str] = None,
    source_name: Optional[str] = None,
    title: Optional[str] = None,
) -> str:
    """
    Serialize PSMs as an mzTab 1.0 document (PSM section only).

    Args:
        psms: PSMs to write.
        search_info: Search metadata for the ``MTD`` section.
        ms_run_location: Spectra file the PSMs refer to (``ms_run[1]``).
        source_format: Format the PSMs were converted from (provenance).
        source_name: File name the PSMs were converted from (provenance).
        title: Optional ``mzTab-ID``.

    Returns:
        The mzTab document as a string.
    """
    psms = list(psms)
    info = search_info or SearchInfo()
    lines: List[str] = []

    def mtd(key: str, value: str) -> None:
        lines.append(f"MTD\t{key}\t{_clean(value)}")

    mtd("mzTab-version", "1.0.0")
    mtd("mzTab-mode", "Summary")
    mtd("mzTab-type", "Identification")
    if title:
        mtd("mzTab-ID", title)
    if source_format:
        description = f"Converted by mscompress from {source_format}"
        if source_name:
            description += f" ({source_name})"
    else:
        description = "PSMs written by mscompress"
    mtd("description", description)
    mtd("ms_run[1]-location", f"file://{ms_run_location}" if ms_run_location else NULL)

    software = _fmt_param(_software_param(info))
    mtd("software[1]", software)
    if info.software_version:
        mtd("software[1]-setting[1]", f"version={info.software_version}")

    for n, param in enumerate(_score_params(info), start=1):
        mtd(f"psm_search_engine_score[{n}]", _fmt_param(param))

    for kind, mods, none_param in (
        ("fixed_mod", info.fixed_mods, _NO_FIXED_MODS),
        ("variable_mod", info.variable_mods, _NO_VARIABLE_MODS),
    ):
        if not mods:
            mtd(f"{kind}[1]", _fmt_param(none_param))
            continue
        for k, mod in enumerate(mods, start=1):
            chemmod = f"CHEMMOD:{_fmt_massdiff(mod.massdiff)}"
            mtd(f"{kind}[{k}]", _fmt_param(("CHEMMOD", chemmod, "", "")))
            mtd(f"{kind}[{k}]-site", mod.site)
            mtd(f"{kind}[{k}]-position", mod.position)

    custom = 0
    if source_format:
        custom += 1
        mtd(f"custom[{custom}]", _fmt_param(("", "", "source_format", source_format)))
    if source_name:
        custom += 1
        mtd(f"custom[{custom}]", _fmt_param(("", "", "source_file", source_name)))

    # Extra keys become columns in first-seen order across all PSMs.
    extra_keys: Dict[str, None] = {}
    for psm in psms:
        for key in psm.extra:
            extra_keys.setdefault(key, None)

    header = PSH_COLUMNS + [
        EXTRA_PREFIX + re.sub(r"\s", "_", key) for key in extra_keys
    ]
    lines.append("PSH\t" + "\t".join(header))

    for psm_id, psm in enumerate(psms, start=1):
        if psm.spectrum_index is not None:
            spectra_ref = f"ms_run[1]:index={int(psm.spectrum_index)}"
        else:
            spectra_ref = f"ms_run[1]:scan={int(psm.scan_number)}"

        unique = NULL if not psm.proteins else ("1" if len(psm.proteins) == 1 else "0")
        before = [_clean(psm.peptide), str(psm_id)]
        after = [
            unique,
            NULL,
            NULL,
            software,
            _fmt_float(psm.score),
            _fmt_float(psm.q_value),
            _fmt_float(psm.pep),
            NULL,
            _fmt_float(psm.retention_time),
            _fmt_int(psm.charge),
            _fmt_float(psm.precursor_mz),
            NULL,
            spectra_ref,
            NULL,
            NULL,
            NULL,
            NULL,
            "1" if psm.is_decoy else "0",
            _clean(psm.modified_peptide) if psm.modified_peptide is not None else NULL,
            _fmt_int(psm.rank),
            _fmt_int(psm.scan_number),
            _fmt_int(psm.spectrum_index),
            _fmt_float(psm.precursor_mass),
            _fmt_float(psm.mass_diff),
            _fmt_int(psm.num_matched_ions),
            _fmt_int(psm.num_total_ions),
        ] + [_fmt_extra(psm.extra.get(key)) for key in extra_keys]

        for accession in psm.proteins or [None]:
            cell = NULL if accession is None else _clean(accession)
            lines.append("\t".join(["PSM"] + before + [cell] + after))

    return "\n".join(lines) + "\n"


def reader_to_mztab(
    reader: BasePSMReader,
    ms_run_location: Optional[str] = None,
    title: Optional[str] = None,
) -> str:
    """
    Convert any PSM reader's contents to mzTab.

    mzTab input is returned unchanged, so re-archiving never rewrites it.
    """
    if isinstance(reader, MzTabReader):
        return reader.source.read().decode("utf-8")
    source_name = reader.name
    if source_name and source_name.lower().endswith(".zst"):
        source_name = source_name[:-4]
    return write_mztab(
        reader.psms,
        search_info=reader.search_info,
        ms_run_location=ms_run_location,
        source_format=reader.format.value,
        source_name=Path(source_name).name if source_name else None,
        title=title,
    )


def mztab_member_name(source_name: str, compressed: bool) -> str:
    """Archive member name for an annotation converted from `source_name`."""
    name = Path(source_name).name
    if name.lower().endswith(".zst"):
        name = name[:-4]
    if not name.lower().endswith(".mztab"):
        name += ".mztab"
    return name + ".zst" if compressed else name


# -- reader ----------------------------------------------------------------


def _parse_param(text: str) -> Optional[Param]:
    text = text.strip()
    if not (text.startswith("[") and text.endswith("]")):
        return None
    parts = [p.strip() for p in text[1:-1].split(",", 3)]
    if len(parts) != 4:
        return None
    return (parts[0], parts[1], parts[2], parts[3])


def _parse_float(text: Optional[str]) -> Optional[float]:
    if text is None:
        return None
    text = text.strip()
    if not text or text.lower() == NULL:
        return None
    if text == "INF":
        return math.inf
    if text == "-INF":
        return -math.inf
    try:
        return float(text)
    except ValueError:
        return None


def _parse_int(text: Optional[str]) -> Optional[int]:
    value = _parse_float(text)
    if value is None or math.isnan(value) or math.isinf(value):
        return None
    return int(value)


def _parse_extra(text: str) -> Any:
    if text.lower() in ("true", "false"):
        return text.lower() == "true"
    value = _parse_float(text)
    return text if value is None else value


class MzTabReader(BasePSMReader):
    """
    Reader for mzTab 1.0 files (PSM section).

    Reads files written by mscompress as well as third-party mzTab: columns
    are looked up by header name and missing optional columns are tolerated.

    Example:
        ```python
        reader = MzTabReader("results.mztab")
        for psm in reader:
            print(psm.peptide, psm.score, psm.q_value)
        ```
    """

    def __init__(self, source: Union[str, Path, BaseAnnotationFile]):
        super().__init__(source)
        self._metadata: Dict[str, str] = {}
        self._source_format: Optional[str] = None
        self._source_name: Optional[str] = None

    @property
    def format(self) -> AnnotationFormat:
        """Return the format identifier."""
        return AnnotationFormat.MZTAB

    @property
    def metadata(self) -> Dict[str, str]:
        """Raw ``MTD`` key/value pairs."""
        self._ensure_parsed()
        return self._metadata

    @property
    def source_format(self) -> Optional[str]:
        """Format the PSMs were converted from, if recorded."""
        self._ensure_parsed()
        return self._source_format

    @property
    def source_name(self) -> Optional[str]:
        """File name the PSMs were converted from, if recorded."""
        self._ensure_parsed()
        return self._source_name

    def _parse(self) -> None:
        """Parse the mzTab file."""
        text = self._source.read().decode("utf-8")
        header: Optional[List[str]] = None
        rows: List[Dict[str, str]] = []

        for line in text.splitlines():
            if not line.strip():
                continue
            cells = line.split("\t")
            tag = cells[0]
            if tag == "MTD" and len(cells) >= 3:
                self._metadata[cells[1]] = cells[2]
            elif tag == "PSH":
                header = cells[1:]
            elif tag == "PSM" and header is not None:
                rows.append(dict(zip(header, cells[1:])))

        roles = self._parse_metadata()

        current_id: Optional[str] = None
        for row in rows:
            psm_id = row.get("PSM_ID")
            accession = row.get("accession", NULL)
            if psm_id is not None and psm_id == current_id and self._psms:
                if accession != NULL and accession not in self._psms[-1].proteins:
                    self._psms[-1].proteins.append(accession)
                continue
            current_id = psm_id
            self._psms.append(self._parse_row(row, roles))

    def _parse_metadata(self) -> Tuple[str, Optional[str], Optional[str]]:
        """
        Fill search info / provenance from MTD and resolve score columns.

        Returns:
            Column names of the (primary score, q-value, PEP) columns.
        """
        info = SearchInfo()
        scores: Dict[int, Param] = {}
        mods: Dict[Tuple[str, int], Dict[str, str]] = {}

        for key, value in self._metadata.items():
            match = _INDEXED_KEY_RE.match(key)
            if not match:
                continue
            name, n, sub = match.group(1), int(match.group(2)), match.group(3)
            param = _parse_param(value)

            if name == "software" and n == 1:
                if sub is None and param is not None:
                    # The generic term with no value is how "unknown" is written.
                    generic = param[1] == "MS:1001456" and not param[3]
                    info.software = None if generic else (param[3] or param[2] or None)
                elif sub and sub.startswith("setting") and value.startswith("version="):
                    info.software_version = value[len("version="):]
            elif name == "psm_search_engine_score" and sub is None and param is not None:
                scores[n] = param
            elif name in ("fixed_mod", "variable_mod"):
                mods.setdefault((name, n), {})[sub or "param"] = value
            elif name == "custom" and sub is None and param is not None:
                if param[2] == "source_format":
                    self._source_format = param[3] or None
                elif param[2] == "source_file":
                    self._source_name = param[3] or None

        for (kind, _), fields in sorted(mods.items(), key=lambda kv: kv[0]):
            param = _parse_param(fields.get("param", ""))
            if param is None or not param[1].startswith("CHEMMOD:"):
                continue
            massdiff = _parse_float(param[1][len("CHEMMOD:"):])
            if massdiff is None:
                continue
            mod = ModificationSpec(
                massdiff=massdiff,
                site=fields.get("site", ""),
                position=fields.get("position", "Anywhere"),
            )
            (info.fixed_mods if kind == "fixed_mod" else info.variable_mods).append(mod)

        primary: Optional[int] = None
        q_value: Optional[int] = None
        pep: Optional[int] = None
        for n in sorted(scores):
            cv, accession, name, _ = scores[n]
            lname = name.lower()
            if accession in _Q_VALUE_ACCESSIONS or "q-value" in lname or "q value" in lname:
                q_value = q_value or n
            elif accession in _PEP_ACCESSIONS or "posterior error" in lname or lname == "pep":
                pep = pep or n
            elif primary is None:
                primary = n
                if accession in _SCORE_NAMES_BY_ACCESSION:
                    info.primary_score_name = _SCORE_NAMES_BY_ACCESSION[accession]
                else:
                    info.primary_score_name = name or None
        if not scores:
            primary = 1

        self._search_info = info

        def column(n: Optional[int]) -> Optional[str]:
            return None if n is None else f"search_engine_score[{n}]"

        return column(primary) or "search_engine_score[1]", column(q_value), column(pep)

    def _parse_row(
        self,
        row: Dict[str, str],
        roles: Tuple[str, Optional[str], Optional[str]],
    ) -> PSM:
        score_col, q_col, pep_col = roles

        def get(name: str) -> Optional[str]:
            value = row.get(name)
            return None if value is None or value == NULL else value

        spectra_ref = row.get("spectra_ref", "")
        scan_match = _SCAN_RE.search(spectra_ref)
        index_match = _INDEX_RE.search(spectra_ref)
        scan_number = _parse_int(get("opt_global_scan_number"))
        if scan_number is None:
            scan_number = int(scan_match.group(1)) if scan_match else 0
        spectrum_index = _parse_int(get("opt_global_spectrum_index"))
        if spectrum_index is None and index_match:
            spectrum_index = int(index_match.group(1))

        retention_time = get("retention_time")
        if retention_time is not None:
            retention_time = retention_time.split("|")[0]

        decoy = (get(DECOY_COLUMN) or get("opt_global_decoy") or "0").lower()
        accession = get("accession")

        extra: Dict[str, Any] = {}
        known = set(PSH_COLUMNS) | {"opt_global_decoy"}
        for name, value in row.items():
            if value == NULL or name in known or not name.startswith("opt_"):
                continue
            if name.startswith(EXTRA_PREFIX):
                key = name[len(EXTRA_PREFIX):]
            elif name.startswith("opt_global_"):
                key = name[len("opt_global_"):]
            else:
                key = name
            extra[key] = _parse_extra(value)

        score = _parse_float(get(score_col))
        return PSM(
            scan_number=scan_number,
            peptide=row.get("sequence", ""),
            charge=_parse_int(get("charge")) or 0,
            score=0.0 if score is None else score,
            proteins=[accession] if accession is not None else [],
            spectrum_index=spectrum_index,
            q_value=_parse_float(get(q_col)) if q_col else None,
            pep=_parse_float(get(pep_col)) if pep_col else None,
            retention_time=_parse_float(retention_time),
            precursor_mz=_parse_float(get("exp_mass_to_charge")),
            precursor_mass=_parse_float(get("opt_global_precursor_mass")),
            mass_diff=_parse_float(get("opt_global_mass_diff")),
            modified_peptide=get("opt_global_modified_sequence"),
            num_matched_ions=_parse_int(get("opt_global_num_matched_ions")),
            num_total_ions=_parse_int(get("opt_global_num_total_ions")),
            rank=_parse_int(get("opt_global_rank")) or 1,
            is_decoy=decoy in ("1", "true"),
            extra=extra,
        )

    def filter_text(self, scan_numbers: Set[int]) -> str:
        """
        Return the document with only PSM rows for the given scan numbers.

        Every non-PSM line is kept verbatim and in order.
        """
        self._ensure_parsed()
        keep: Set[int] = set()
        for psm_index, psm in enumerate(self._psms):
            if psm.scan_number in scan_numbers:
                keep.add(psm_index)

        text = self._source.read().decode("utf-8")
        out: List[str] = []
        header: Optional[List[str]] = None
        psm_index = -1
        current_id: Optional[str] = None
        for line in text.splitlines():
            cells = line.split("\t")
            if cells[0] == "PSH":
                header = cells[1:]
            if cells[0] != "PSM" or header is None:
                out.append(line)
                continue
            psm_id = dict(zip(header, cells[1:])).get("PSM_ID")
            if psm_id is None or psm_id != current_id or psm_index < 0:
                psm_index += 1
                current_id = psm_id
            if psm_index in keep:
                out.append(line)
        return "\n".join(out) + "\n"

    def filter_to_file(self, output_path: Union[str, Path], scan_numbers: Set[int]) -> None:
        """
        Write a subset of PSMs matching the given scan numbers to a new file.

        Args:
            output_path: Path to the output file.
            scan_numbers: Set of scan numbers to include.
        """
        Path(output_path).write_text(self.filter_text(scan_numbers), encoding="utf-8")
