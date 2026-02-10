#!/usr/bin/env python3
"""Parse a Valgrind Memcheck report into a concise Markdown summary.

Usage:
    python parse_valgrind.py <input_file> [output_file]

If output_file is omitted, writes to stdout.
"""

import re
import sys
from collections import defaultdict
from pathlib import Path


def human_bytes(n: int) -> str:
    """Format byte count to human-readable string."""
    for unit in ("B", "KB", "MB", "GB"):
        if abs(n) < 1024:
            return f"{n:,.0f} {unit}" if unit == "B" else f"{n:,.1f} {unit}"
        n /= 1024
    return f"{n:,.1f} TB"


def parse_valgrind(text: str) -> str:
    # Strip PID prefixes
    lines = [re.sub(r'^[=-]+\d+[=-]+\s*', '', l) for l in text.splitlines()]

    # --- Header info ---
    cmd = ""
    for l in lines:
        if l.startswith("Command:"):
            cmd = l.split(":", 1)[1].strip()
            break

    # --- Heap summary ---
    heap = {}
    for l in lines:
        m = re.match(r'in use at exit:\s+([\d,]+)\s+bytes\s+in\s+([\d,]+)\s+blocks', l)
        if m:
            heap['exit_bytes'] = int(m.group(1).replace(',', ''))
            heap['exit_blocks'] = int(m.group(2).replace(',', ''))
        m = re.match(r'total heap usage:\s+([\d,]+)\s+allocs,\s+([\d,]+)\s+frees,\s+([\d,]+)\s+bytes', l)
        if m:
            heap['allocs'] = int(m.group(1).replace(',', ''))
            heap['frees'] = int(m.group(2).replace(',', ''))
            heap['total_bytes'] = int(m.group(3).replace(',', ''))

    # --- Leak summary ---
    leak = {}
    for l in lines:
        m = re.match(r'(definitely lost|indirectly lost|possibly lost|still reachable|suppressed):\s+([\d,]+)\s+bytes\s+in\s+([\d,]+)\s+blocks', l)
        if m:
            leak[m.group(1)] = {
                'bytes': int(m.group(2).replace(',', '')),
                'blocks': int(m.group(3).replace(',', ''))
            }

    # --- Error summary ---
    error_count = 0
    error_contexts = 0
    for l in lines:
        m = re.match(r'ERROR SUMMARY:\s+(\d+)\s+errors\s+from\s+(\d+)\s+contexts', l)
        if m:
            error_count = int(m.group(1))
            error_contexts = int(m.group(2))

    # --- Parse individual loss records ---
    # Rejoin for block parsing
    full = '\n'.join(lines)

    # Pattern: "<bytes> (<detail>) bytes in <blocks> blocks are <kind> in loss record ..."
    record_pattern = re.compile(
        r'([\d,]+(?:\s+\([\d,]+\s+direct,\s+[\d,]+\s+indirect\))?)\s+bytes?\s+in\s+([\d,]+)\s+blocks?\s+are\s+'
        r'(definitely lost|possibly lost|indirectly lost|still reachable)\s+in\s+loss record\s+[\d,]+\s+of\s+[\d,]+\s*\n'
        r'((?:(?:at|by)\s+0x[0-9A-Fa-f]+:.*\n)*)',
        re.MULTILINE
    )

    records = []
    for m in record_pattern.finditer(full):
        raw_bytes_str = m.group(1)
        # Parse total bytes (including indirect)
        direct_m = re.match(r'([\d,]+)\s+\(([\d,]+)\s+direct,\s+([\d,]+)\s+indirect\)', raw_bytes_str)
        if direct_m:
            total_bytes = int(direct_m.group(1).replace(',', ''))
            direct_bytes = int(direct_m.group(2).replace(',', ''))
            indirect_bytes = int(direct_m.group(3).replace(',', ''))
        else:
            total_bytes = int(raw_bytes_str.replace(',', ''))
            direct_bytes = total_bytes
            indirect_bytes = 0

        blocks = int(m.group(2).replace(',', ''))
        kind = m.group(3)
        stack_text = m.group(4)

        # Parse stack frames - keep only user code (with file:line), skip runtime/valgrind
        frames = []
        for frame_line in stack_text.strip().split('\n'):
            frame_line = frame_line.strip()
            # Extract function and source
            fm = re.match(r'(?:at|by)\s+0x[0-9A-Fa-f]+:\s+(.+)', frame_line)
            if fm:
                detail = fm.group(1)
                # Skip valgrind internals and Python/system runtime
                if any(skip in detail for skip in ['vgpreload_memcheck', 'libpython', '_PyObject_', '_PyEval_', 'PyObject_', 'start_thread', 'clone (']):
                    continue
                # Extract function name and optional source
                src_m = re.match(r'(\S+)\s+\((\S+:\d+)\)', detail)
                if src_m:
                    frames.append((src_m.group(1), src_m.group(2)))
                else:
                    # Check for (in ...) pattern - skip these (library frames)
                    if '(in ' in detail:
                        continue
                    frames.append((detail.split('(')[0].strip(), ''))

        records.append({
            'total_bytes': total_bytes,
            'direct_bytes': direct_bytes,
            'indirect_bytes': indirect_bytes,
            'blocks': blocks,
            'kind': kind,
            'frames': frames,
        })

    # --- Deduplicate/group records by stack signature ---
    def stack_sig(rec):
        return tuple((f, s) for f, s in rec['frames'][:4])

    groups = defaultdict(lambda: {'total_bytes': 0, 'blocks': 0, 'kind': '', 'frames': [], 'count': 0})
    for r in records:
        sig = stack_sig(r)
        g = groups[sig]
        g['total_bytes'] += r['total_bytes']
        g['blocks'] += r['blocks']
        g['kind'] = r['kind']
        g['frames'] = r['frames']
        g['count'] += 1

    # Sort by total bytes descending
    sorted_groups = sorted(groups.values(), key=lambda g: g['total_bytes'], reverse=True)

    # --- Build markdown ---
    md = []
    md.append("# Valgrind Memcheck Summary\n")

    if cmd:
        md.append(f"**Command:** `{cmd}`\n")

    # Heap overview
    if heap:
        unfreed = heap.get('allocs', 0) - heap.get('frees', 0)
        md.append("## Heap Overview\n")
        md.append(f"| Metric | Value |")
        md.append(f"|--------|-------|")
        md.append(f"| In use at exit | {human_bytes(heap.get('exit_bytes', 0))} in {heap.get('exit_blocks', 0):,} blocks |")
        md.append(f"| Total allocated | {human_bytes(heap.get('total_bytes', 0))} |")
        md.append(f"| Allocs / Frees | {heap.get('allocs', 0):,} / {heap.get('frees', 0):,} (unfreed: {unfreed:,}) |")
        md.append("")

    # Leak summary
    if leak:
        md.append("## Leak Summary\n")
        md.append("| Category | Bytes | Blocks |")
        md.append("|----------|-------|--------|")
        for cat in ['definitely lost', 'indirectly lost', 'possibly lost', 'still reachable', 'suppressed']:
            if cat in leak:
                sev = "🔴" if cat == "definitely lost" else "🟡" if cat == "possibly lost" else "🟠" if cat == "indirectly lost" else "⚪"
                md.append(f"| {sev} {cat} | {human_bytes(leak[cat]['bytes'])} | {leak[cat]['blocks']:,} |")
        md.append(f"\n**Errors:** {error_count} from {error_contexts} contexts\n")

    # Top leak sources
    if sorted_groups:
        # Show top 15 or all if fewer
        top_n = min(15, len(sorted_groups))
        md.append(f"## Top {top_n} Leak Sources (by bytes)\n")

        for i, g in enumerate(sorted_groups[:top_n], 1):
            kind_tag = "DEF" if "definitely" in g['kind'] else "POS" if "possibly" in g['kind'] else g['kind'][:3].upper()
            count_str = f" ×{g['count']}" if g['count'] > 1 else ""
            md.append(f"### {i}. [{kind_tag}] {human_bytes(g['total_bytes'])} in {g['blocks']:,} blocks{count_str}\n")

            if g['frames']:
                md.append("```")
                for func, src in g['frames'][:6]:
                    if src:
                        md.append(f"  {func}  ({src})")
                    else:
                        md.append(f"  {func}")
                md.append("```")
            md.append("")

    return '\n'.join(md)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <valgrind_log> [output.md]", file=sys.stderr)
        sys.exit(1)

    input_path = Path(sys.argv[1])
    text = input_path.read_text(errors='replace')
    result = parse_valgrind(text)

    if len(sys.argv) >= 3:
        out = Path(sys.argv[2])
        out.write_text(result)
        print(f"Written to {out}", file=sys.stderr)
    else:
        print(result)


if __name__ == '__main__':
    main()