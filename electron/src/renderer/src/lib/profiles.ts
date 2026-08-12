// Compression presets shown in the Convert UI and Settings dialog. The ids match
// the shared `Preset` union (and COMPRESSION_PRESETS) in src/shared/ipc.ts.
import type { Preset } from "@shared/ipc"

export const compressionProfiles: { id: Preset; label: string; blurb: string }[] = [
  { id: "fastest", label: "Fastest", blurb: "Lowest latency, larger output" },
  { id: "fast", label: "Fast", blurb: "Quick, good ratio" },
  { id: "default", label: "Default", blurb: "Balanced (recommended)" },
  { id: "better", label: "Better", blurb: "Smallest output, slower" },
]
