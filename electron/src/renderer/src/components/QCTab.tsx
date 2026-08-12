import { useEffect, useRef, useState } from "react"
import { Loader2, AlertTriangle } from "lucide-react"
import {
  Area,
  AreaChart,
  Bar,
  BarChart,
  Cell,
  Pie,
  PieChart,
  ResponsiveContainer,
  Tooltip as RTooltip,
  XAxis,
  YAxis,
  CartesianGrid,
} from "recharts"
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import type { FileKind, QCData } from "@shared/ipc"

const donutColors = ["var(--chart-1)", "var(--chart-2)", "var(--chart-4)"]

// viridis-ish ramp for density 0..1 → [r, g, b]
function heatRGB(t: number): [number, number, number] {
  const stops = [
    [68, 1, 84],
    [59, 82, 139],
    [33, 145, 140],
    [94, 201, 98],
    [253, 231, 37],
  ]
  const x = Math.max(0, Math.min(1, t)) * (stops.length - 1)
  const i = Math.floor(x)
  const f = x - i
  const a = stops[i]
  const b = stops[Math.min(i + 1, stops.length - 1)]
  return [
    Math.round(a[0] + (b[0] - a[0]) * f),
    Math.round(a[1] + (b[1] - a[1]) * f),
    Math.round(a[2] + (b[2] - a[2]) * f),
  ]
}

function heatColor(t: number) {
  const [r, g, bl] = heatRGB(t)
  return `rgb(${r},${g},${bl})`
}

/**
 * RT × m/z density map painted to a <canvas> as an ImageData buffer — one pixel
 * per grid cell (rtBins × mzBins), viridis-ramped and normalized by max density.
 * CSS scales it to full width, so the fine grid reads as a continuous 2D image.
 */
function DensityHeatmap({ heatmap }: { heatmap: QCData["heatmap"] }) {
  const canvasRef = useRef<HTMLCanvasElement | null>(null)
  const { rtBins, mzBins, cells, rtMax, mzMin, mzMax } = heatmap
  const maxDensity = cells.reduce((m, c) => Math.max(m, c.density), 0) || 1

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const ctx = canvas.getContext("2d")
    if (!ctx) return

    const img = ctx.createImageData(rtBins, mzBins)
    const data = img.data
    // Base fill = zero-density colour (also covers the empty/no-cells case).
    const [br, bg, bb] = heatRGB(0)
    for (let i = 0; i < rtBins * mzBins; i++) {
      data[i * 4] = br
      data[i * 4 + 1] = bg
      data[i * 4 + 2] = bb
      data[i * 4 + 3] = 255
    }
    // Paint each non-zero cell (rows = m/z, cols = RT), matching the prior grid.
    for (const c of cells) {
      const col = Math.min(rtBins - 1, Math.round((c.rt / (rtMax || 1)) * (rtBins - 1)))
      const row = Math.min(
        mzBins - 1,
        Math.round(((c.mz - mzMin) / (mzMax - mzMin || 1)) * (mzBins - 1)),
      )
      const [r, g, bl] = heatRGB(c.density / maxDensity)
      const idx = (row * rtBins + col) * 4
      data[idx] = r
      data[idx + 1] = g
      data[idx + 2] = bl
      data[idx + 3] = 255
    }
    ctx.putImageData(img, 0, 0)
  }, [rtBins, mzBins, cells, maxDensity, rtMax, mzMin, mzMax])

  return (
    <div className="flex gap-2">
      {/* m/z axis labels */}
      <div className="mono flex flex-col justify-between py-0.5 text-[9px] text-muted-foreground">
        <span>{mzMax.toFixed(0)}</span>
        <span>m/z</span>
        <span>{mzMin.toFixed(0)}</span>
      </div>
      <div className="flex-1">
        <canvas
          ref={canvasRef}
          width={rtBins}
          height={mzBins}
          className="block h-auto w-full rounded-sm bg-border"
        />
        <div className="mono mt-1 flex justify-between text-[9px] text-muted-foreground">
          <span>RT 0</span>
          <span>RT {rtMax.toFixed(2)} min</span>
        </div>
      </div>
    </div>
  )
}

const chartTooltip = {
  contentStyle: {
    background: "var(--popover)",
    border: "1px solid var(--border)",
    borderRadius: "6px",
    fontSize: "11px",
    color: "var(--popover-foreground)",
  },
  labelStyle: { color: "var(--muted-foreground)" },
}

function Panel({
  title,
  right,
  children,
  className = "",
}: {
  title: string
  right?: React.ReactNode
  children: React.ReactNode
  className?: string
}) {
  return (
    <Card className={`gap-2 py-3 ${className}`}>
      <CardHeader className="flex flex-row items-center justify-between px-4">
        <CardTitle className="text-xs font-semibold uppercase tracking-wider text-muted-foreground">
          {title}
        </CardTitle>
        {right}
      </CardHeader>
      <CardContent className="px-3">{children}</CardContent>
    </Card>
  )
}

export function QCTab({ path, name, kind }: { path: string; name: string; kind: FileKind }) {
  const [qc, setQc] = useState<QCData | null>(null)
  const [loading, setLoading] = useState(true)

  // Batch (v2) .mszx: QC runs on ONE archive member, chosen via the dropdown.
  // null = still resolving the manifest; [] = not a batch archive.
  const [batchEntries, setBatchEntries] = useState<string[] | null>(null)
  const [entry, setEntry] = useState<string | null>(null)

  useEffect(() => {
    setBatchEntries(null)
    setEntry(null)
    if (kind !== "mszx") return
    let active = true
    window.api
      .readMszx(path)
      .then((m) => {
        if (!active) return
        if (!m.error && m.container === "batch") {
          const names = m.entries.map((e) => e.entry)
          setBatchEntries(names)
          setEntry(names[0] ?? null)
        } else {
          setBatchEntries([])
        }
      })
      .catch(() => {
        if (active) setBatchEntries([])
      })
    return () => {
      active = false
    }
  }, [path, kind])

  useEffect(() => {
    // For .mszx, wait until the manifest resolved; for a batch archive, wait
    // until a member is picked (batch QC is always per-member).
    if (kind === "mszx" && batchEntries === null) return
    if (batchEntries && batchEntries.length > 0 && !entry) return
    let active = true
    setLoading(true)
    setQc(null)
    window.api
      .computeQC(path, entry ? { entry } : undefined)
      .then((data) => {
        if (!active) return
        setQc(data)
        console.log(
          "[renderer] QC:",
          data.path,
          entry ? `entry=${entry}` : "",
          "tic=",
          data.tic.length,
          "cells=",
          data.heatmap.cells.length,
        )
      })
      .catch((e) => {
        if (active) setQc({ error: String(e) } as QCData)
      })
      .finally(() => {
        if (active) setLoading(false)
      })
    return () => {
      active = false
    }
  }, [path, kind, batchEntries, entry])

  // Member picker, shown only for batch archives (above every QC state).
  const memberSelector =
    batchEntries && batchEntries.length > 0 && entry ? (
      <div className="flex items-center gap-2 px-4 pt-3">
        <span className="text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">
          Archive member
        </span>
        <Select value={entry} onValueChange={setEntry}>
          <SelectTrigger data-testid="qc-entry-select" className="w-64">
            <SelectValue />
          </SelectTrigger>
          <SelectContent>
            {batchEntries.map((e) => (
              <SelectItem key={e} value={e} className="mono">
                {e}
              </SelectItem>
            ))}
          </SelectContent>
        </Select>
      </div>
    ) : null

  if (loading) {
    return (
      <div>
        {memberSelector}
        <div className="flex h-[60vh] flex-col items-center justify-center gap-2 text-muted-foreground">
          <Loader2 className="size-6 animate-spin" />
          <span className="text-xs">
            Computing QC for {entry ?? name}…
          </span>
        </div>
      </div>
    )
  }

  if (!qc || qc.error) {
    return (
      <div>
        {memberSelector}
        <div className="flex h-[60vh] flex-col items-center justify-center gap-2 p-6 text-center text-destructive">
          <AlertTriangle className="size-6" />
          <span className="text-sm font-medium">QC failed</span>
          <p className="mono max-w-md text-[11px] text-muted-foreground">
            {qc?.error ?? "No data"}
          </p>
        </div>
      </div>
    )
  }

  const { heatmap, tic, bpc, msLevelCounts, peaksPerSpectrum } = qc

  const donutData = msLevelCounts.map((m) => ({ name: m.level, value: m.count }))
  const totalSpectra = donutData.reduce((a, b) => a + b.value, 0) || 1

  return (
    <div>
      {memberSelector}
      <div data-testid="qc-charts" className="grid grid-cols-2 gap-3 p-4">
      {/* TIC */}
      <Panel
        title="TIC Chromatogram"
        right={<span className="mono text-[10px] text-muted-foreground">intensity · RT(min)</span>}
      >
        <ResponsiveContainer width="100%" height={160}>
          <AreaChart data={tic} margin={{ top: 5, right: 8, left: 0, bottom: 0 }}>
            <defs>
              <linearGradient id="ticFill" x1="0" y1="0" x2="0" y2="1">
                <stop offset="0%" stopColor="var(--chart-1)" stopOpacity={0.5} />
                <stop offset="100%" stopColor="var(--chart-1)" stopOpacity={0} />
              </linearGradient>
            </defs>
            <CartesianGrid strokeDasharray="3 3" stroke="var(--border)" vertical={false} />
            <XAxis
              dataKey="rt"
              tick={{ fontSize: 9, fill: "var(--muted-foreground)" }}
              tickLine={false}
              axisLine={false}
              tickFormatter={(v) => Number(v).toFixed(2)}
            />
            <YAxis
              tick={{ fontSize: 9, fill: "var(--muted-foreground)" }}
              tickLine={false}
              axisLine={false}
              width={38}
              tickFormatter={(v) => `${(v / 1e6).toFixed(0)}M`}
            />
            <RTooltip
              {...chartTooltip}
              formatter={(v) => [`${(Number(v) / 1e6).toFixed(2)} M`, "TIC"]}
            />
            <Area
              type="monotone"
              dataKey="tic"
              stroke="var(--chart-1)"
              strokeWidth={1.5}
              fill="url(#ticFill)"
            />
          </AreaChart>
        </ResponsiveContainer>
      </Panel>

      {/* Base peak */}
      <Panel
        title="Base-Peak Chromatogram"
        right={<span className="mono text-[10px] text-muted-foreground">BPC · RT(min)</span>}
      >
        <ResponsiveContainer width="100%" height={160}>
          <AreaChart data={bpc} margin={{ top: 5, right: 8, left: 0, bottom: 0 }}>
            <defs>
              <linearGradient id="bpcFill" x1="0" y1="0" x2="0" y2="1">
                <stop offset="0%" stopColor="var(--chart-2)" stopOpacity={0.5} />
                <stop offset="100%" stopColor="var(--chart-2)" stopOpacity={0} />
              </linearGradient>
            </defs>
            <CartesianGrid strokeDasharray="3 3" stroke="var(--border)" vertical={false} />
            <XAxis
              dataKey="rt"
              tick={{ fontSize: 9, fill: "var(--muted-foreground)" }}
              tickLine={false}
              axisLine={false}
              tickFormatter={(v) => Number(v).toFixed(2)}
            />
            <YAxis
              tick={{ fontSize: 9, fill: "var(--muted-foreground)" }}
              tickLine={false}
              axisLine={false}
              width={38}
              tickFormatter={(v) => `${(v / 1e6).toFixed(0)}M`}
            />
            <RTooltip
              {...chartTooltip}
              formatter={(v) => [`${(Number(v) / 1e6).toFixed(2)} M`, "BPC"]}
            />
            <Area
              type="monotone"
              dataKey="bpc"
              stroke="var(--chart-2)"
              strokeWidth={1.5}
              fill="url(#bpcFill)"
            />
          </AreaChart>
        </ResponsiveContainer>
      </Panel>

      {/* Heatmap - spans full width */}
      <Panel
        title="RT × m/z Density Map"
        className="col-span-2"
        right={
          <div className="flex items-center gap-2">
            <span className="mono text-[10px] text-muted-foreground">low</span>
            <div
              className="h-2.5 w-28 rounded-sm"
              style={{
                background: `linear-gradient(to right, ${heatColor(0)}, ${heatColor(0.25)}, ${heatColor(0.5)}, ${heatColor(0.75)}, ${heatColor(1)})`,
              }}
            />
            <span className="mono text-[10px] text-muted-foreground">high</span>
          </div>
        }
      >
        <DensityHeatmap heatmap={heatmap} />
      </Panel>

      {/* MS-level donut */}
      <Panel title="MS-Level Distribution">
        <div className="flex items-center gap-4">
          <ResponsiveContainer width="55%" height={150}>
            <PieChart>
              <Pie
                data={donutData}
                dataKey="value"
                nameKey="name"
                innerRadius={40}
                outerRadius={62}
                paddingAngle={2}
                stroke="var(--background)"
                strokeWidth={2}
              >
                {donutData.map((_, i) => (
                  <Cell key={i} fill={donutColors[i % donutColors.length]} />
                ))}
              </Pie>
              <RTooltip {...chartTooltip} formatter={(v) => Number(v).toLocaleString()} />
            </PieChart>
          </ResponsiveContainer>
          <div className="flex-1 space-y-2">
            {donutData.map((d, i) => (
              <div key={d.name} className="flex items-center gap-2 text-xs">
                <span
                  className="size-2.5 rounded-sm"
                  style={{ background: donutColors[i % donutColors.length] }}
                />
                <span className="text-muted-foreground">{d.name}</span>
                <span className="mono ml-auto">{d.value.toLocaleString()}</span>
                <span className="mono w-10 text-right text-muted-foreground">
                  {((d.value / totalSpectra) * 100).toFixed(1)}%
                </span>
              </div>
            ))}
          </div>
        </div>
      </Panel>

      {/* Peaks histogram */}
      <Panel title="Peaks per Spectrum">
        <ResponsiveContainer width="100%" height={150}>
          <BarChart data={peaksPerSpectrum} margin={{ top: 5, right: 8, left: 0, bottom: 0 }}>
            <CartesianGrid strokeDasharray="3 3" stroke="var(--border)" vertical={false} />
            <XAxis
              dataKey="bin"
              tick={{ fontSize: 8, fill: "var(--muted-foreground)" }}
              tickLine={false}
              axisLine={false}
              interval={0}
              angle={-25}
              textAnchor="end"
              height={40}
            />
            <YAxis
              tick={{ fontSize: 9, fill: "var(--muted-foreground)" }}
              tickLine={false}
              axisLine={false}
              width={38}
              allowDecimals={false}
            />
            <RTooltip
              {...chartTooltip}
              formatter={(v) => [Number(v).toLocaleString(), "spectra"]}
            />
            <Bar dataKey="count" fill="var(--chart-4)" radius={[2, 2, 0, 0]} />
          </BarChart>
        </ResponsiveContainer>
      </Panel>
      </div>
    </div>
  )
}
