import { Info, HardDrive, Layers, Cpu, FlaskConical } from "lucide-react"
import { Badge } from "@/components/ui/badge"
import { Separator } from "@/components/ui/separator"
import { ScrollArea } from "@/components/ui/scroll-area"
import { sampleAnalysis, fmtBytes } from "@/mockData"

function Row({ label, value }: { label: string; value: React.ReactNode }) {
  return (
    <div className="flex items-baseline justify-between gap-3 py-1">
      <span className="text-[11px] text-muted-foreground">{label}</span>
      <span className="mono text-right text-xs">{value}</span>
    </div>
  )
}

function GroupTitle({ icon: Icon, children }: { icon: React.ElementType; children: React.ReactNode }) {
  return (
    <div className="flex items-center gap-1.5 px-3 pt-3 pb-1 text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">
      <Icon className="size-3" /> {children}
    </div>
  )
}

export function Inspector({ selected }: { selected: string }) {
  const a = sampleAnalysis
  const totalMs = a.msLevelCounts.reduce((s, m) => s + m.count, 0)
  const estCompressed = a.filesizeBytes * 0.28

  return (
    <div className="flex h-full flex-col bg-sidebar text-sidebar-foreground">
      <div className="flex items-center gap-1.5 px-3 py-2 text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">
        <Info className="size-3.5" /> Inspector
      </div>
      <Separator />

      <div className="px-3 py-2.5">
        <div className="truncate text-sm font-medium">{selected}</div>
        <div className="mt-1 flex items-center gap-1.5">
          <Badge variant="outline" className="mono text-[10px]">{a.kind}</Badge>
          <Badge variant="secondary" className="mono text-[10px]">{a.sourceCompression}</Badge>
        </div>
      </div>
      <Separator />

      <ScrollArea className="min-h-0 flex-1">
        <GroupTitle icon={HardDrive}>Storage</GroupTitle>
        <div className="px-3">
          <Row label="Filesize" value={fmtBytes(a.filesizeBytes)} />
          <Row label="Source compression" value={a.sourceCompression} />
          <Row
            label="Est. compressed"
            value={<span className="text-emerald-400">{fmtBytes(estCompressed)}</span>}
          />
          <Row label="Est. ratio" value={<span className="text-emerald-400">×3.6</span>} />
        </div>
        <Separator className="my-1" />

        <GroupTitle icon={FlaskConical}>Spectra</GroupTitle>
        <div className="px-3">
          <Row label="Spectrum count" value={a.spectrumCount.toLocaleString()} />
          <Row label="m/z format" value={a.mzFormat} />
          <Row label="Intensity format" value={a.intensityFormat} />
          <Row
            label="RT range"
            value={`${a.rtRangeSec[0]}–${(a.rtRangeSec[1] / 60).toFixed(0)}m`}
          />
        </div>
        <Separator className="my-1" />

        <GroupTitle icon={Layers}>MS-Level Breakdown</GroupTitle>
        <div className="px-3 pb-2">
          {a.msLevelCounts.map((m) => {
            const pct = (m.count / totalMs) * 100
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
          <Row label="Threads" value={`${a.threads} / 16`} />
          <Row label="Backend" value="mscompress 2.4.1" />
        </div>
      </ScrollArea>
    </div>
  )
}
