import { useEffect, useState } from "react"
import { Info, HardDrive, Layers, Cpu, FlaskConical, AlertTriangle } from "lucide-react"
import { Badge } from "@/components/ui/badge"
import { Separator } from "@/components/ui/separator"
import { ScrollArea } from "@/components/ui/scroll-area"
import { Skeleton } from "@/components/ui/skeleton"
import { fmtBytes } from "@/lib/format"
import type { FileKind } from "@shared/ipc"
import type { FileSummary } from "@shared/ipc"

function Row({ label, value }: { label: string; value: React.ReactNode }) {
  return (
    <div className="flex items-baseline justify-between gap-3 py-1">
      <span className="text-[11px] text-muted-foreground">{label}</span>
      <span className="selectable mono text-right text-xs">{value}</span>
    </div>
  )
}

function GroupTitle({
  icon: Icon,
  children,
}: {
  icon: React.ElementType
  children: React.ReactNode
}) {
  return (
    <div className="flex items-center gap-1.5 px-3 pt-3 pb-1 text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">
      <Icon className="size-3" /> {children}
    </div>
  )
}

// Retention-time span → friendly string. Seconds under 2 min, minutes above.
function fmtRtRange([a, b]: [number, number]): string {
  if (a === 0 && b === 0) return "—"
  const f = (s: number) => (s >= 120 ? `${(s / 60).toFixed(1)} min` : `${s.toFixed(1)} s`)
  return `${f(a)} – ${f(b)}`
}

export function Inspector({ path, name, kind }: { path: string; name: string; kind: FileKind }) {
  const [summary, setSummary] = useState<FileSummary | null>(null)
  const [loading, setLoading] = useState(true)
  const [threads, setThreads] = useState<number | null>(null)
  const [version, setVersion] = useState<string | null>(null)

  // Re-analyze whenever the selected file changes. `active` guards against a
  // stale in-flight result overwriting a newer selection.
  useEffect(() => {
    let active = true
    setLoading(true)
    setSummary(null)
    window.api
      .analyze(path)
      .then((s) => {
        if (!active) return
        setSummary(s)
        console.log("[renderer] inspector analyze:", s.fileName, "spectra=", s.spectrumCount)
      })
      .catch((e) => {
        if (!active) return
        setSummary({
          path,
          fileName: name,
          kind,
          filesizeBytes: 0,
          spectrumCount: null,
          mzFormat: null,
          intensityFormat: null,
          sourceCompression: null,
          msLevelCounts: [],
          rtRangeSec: [0, 0],
          accessions: null,
          error: String(e),
        })
      })
      .finally(() => {
        if (active) setLoading(false)
      })
    return () => {
      active = false
    }
  }, [path, name, kind])

  useEffect(() => {
    window.api
      .getNumThreads()
      .then(setThreads)
      .catch(() => {})
    window.api
      .getVersion()
      .then(setVersion)
      .catch(() => {})
  }, [])

  const a = summary
  const totalMs = a?.msLevelCounts.reduce((s, m) => s + m.count, 0) ?? 0

  return (
    <div
      data-testid="inspector"
      className="flex h-full flex-col bg-sidebar text-sidebar-foreground"
    >
      <div className="flex items-center gap-1.5 px-3 py-2 text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">
        <Info className="size-3.5" /> Inspector
      </div>
      <Separator />

      <div className="px-3 py-2.5">
        <div className="selectable truncate text-sm font-medium">{name}</div>
        <div className="mt-1 flex items-center gap-1.5">
          <Badge variant="outline" className="mono text-[10px]">
            {kind}
          </Badge>
          {a?.sourceCompression && (
            <Badge variant="secondary" className="mono text-[10px]">
              {a.sourceCompression}
            </Badge>
          )}
        </div>
      </div>
      <Separator />

      <ScrollArea className="min-h-0 flex-1">
        {loading ? (
          <div className="space-y-2 p-3">
            <Skeleton className="h-3 w-24" />
            <Skeleton className="h-3 w-full" />
            <Skeleton className="h-3 w-5/6" />
            <Skeleton className="h-3 w-2/3" />
            <Skeleton className="mt-3 h-3 w-24" />
            <Skeleton className="h-3 w-full" />
            <Skeleton className="h-3 w-4/6" />
          </div>
        ) : a?.error ? (
          <div className="m-3 rounded-md border border-destructive/40 bg-destructive/10 p-3">
            <div className="flex items-center gap-1.5 text-xs font-medium text-destructive">
              <AlertTriangle className="size-3.5" /> Analysis failed
            </div>
            <p className="mono mt-1.5 text-[11px] leading-relaxed text-muted-foreground">
              {a.error}
            </p>
            <Row label="Filesize" value={fmtBytes(a.filesizeBytes)} />
          </div>
        ) : a ? (
          <>
            <GroupTitle icon={HardDrive}>Storage</GroupTitle>
            <div className="px-3">
              <Row label="Filesize" value={fmtBytes(a.filesizeBytes)} />
              <Row label="Source compression" value={a.sourceCompression ?? "—"} />
              {kind === "mzML" && (
                <>
                  <Row
                    label="Est. compressed"
                    value={
                      <span className="text-emerald-400">{fmtBytes(a.filesizeBytes * 0.28)}</span>
                    }
                  />
                  <Row label="Est. ratio" value={<span className="text-emerald-400">×3.6</span>} />
                </>
              )}
            </div>
            <Separator className="my-1" />

            <GroupTitle icon={FlaskConical}>Spectra</GroupTitle>
            <div className="px-3">
              <Row
                label="Spectrum count"
                value={a.spectrumCount != null ? a.spectrumCount.toLocaleString() : "—"}
              />
              <Row label="m/z format" value={a.mzFormat ?? "—"} />
              <Row label="Intensity format" value={a.intensityFormat ?? "—"} />
              <Row label="RT range" value={fmtRtRange(a.rtRangeSec)} />
            </div>
            <Separator className="my-1" />

            <GroupTitle icon={Layers}>MS-Level Breakdown</GroupTitle>
            <div className="px-3 pb-2">
              {a.msLevelCounts.length === 0 && (
                <div className="py-1 text-[11px] text-muted-foreground">No level data</div>
              )}
              {a.msLevelCounts.map((m) => {
                const pct = totalMs ? (m.count / totalMs) * 100 : 0
                return (
                  <div key={m.level} className="py-1">
                    <div className="flex items-baseline justify-between">
                      <span className="text-[11px] text-muted-foreground">{m.level}</span>
                      <span className="mono text-xs">
                        {m.count.toLocaleString()}{" "}
                        <span className="text-muted-foreground">({pct.toFixed(1)}%)</span>
                      </span>
                    </div>
                    <div className="mt-1 h-1 w-full overflow-hidden rounded-full bg-muted">
                      <div
                        className="h-full rounded-full bg-chart-1"
                        style={{ width: `${pct}%` }}
                      />
                    </div>
                  </div>
                )
              })}
            </div>
            <Separator className="my-1" />

            <GroupTitle icon={Cpu}>Runtime</GroupTitle>
            <div className="px-3 pb-4">
              <Row label="Threads" value={threads != null ? `${threads}` : "…"} />
              <Row label="Backend" value={version ? `mscompress ${version}` : "…"} />
            </div>
          </>
        ) : null}
      </ScrollArea>
    </div>
  )
}
