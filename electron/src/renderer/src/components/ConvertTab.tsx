import { useEffect, useState } from "react"
import {
  FileArchive,
  FileDown,
  Scissors,
  FolderOpen,
  FolderSearch,
  Zap,
  HardDrive,
  Server,
  Loader2,
  CheckCircle2,
  AlertTriangle,
} from "lucide-react"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Slider } from "@/components/ui/slider"
import { Progress } from "@/components/ui/progress"
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import { Tabs, TabsList, TabsTrigger, TabsContent } from "@/components/ui/tabs"
import {
  ToggleGroup,
  ToggleGroupItem,
} from "@/components/ui/toggle-group"
import {
  Accordion,
  AccordionContent,
  AccordionItem,
  AccordionTrigger,
} from "@/components/ui/accordion"
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip"
import { compressionProfiles, type FileKind } from "@/mockData"
import { fmtBytes } from "@/mockData"
import { COMPRESSION_PRESETS } from "@shared/ipc"
import type {
  ConvertResult,
  ExtractMode,
  LossyAlgo,
  Preset,
} from "@shared/ipc"

type Op = "compress" | "decompress" | "extract"

const lossyAlgos: LossyAlgo[] = ["none", "cast", "log", "delta16", "delta32", "vbr"]

// Which operations make sense for each source file kind.
const opsForKind: Record<FileKind, Record<Op, boolean>> = {
  mzML: { compress: true, decompress: false, extract: true },
  msz: { compress: false, decompress: true, extract: true },
  mszx: { compress: false, decompress: true, extract: true },
}

function SectionLabel({ children }: { children: React.ReactNode }) {
  return (
    <div className="text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">
      {children}
    </div>
  )
}

export function ConvertTab({ kind, path }: { kind: FileKind; path: string }) {
  const allowed = opsForKind[kind]

  const [op, setOp] = useState<Op>("compress")
  const [profile, setProfile] = useState<Preset>("default")
  const [mzAlgo, setMzAlgo] = useState<LossyAlgo>(COMPRESSION_PRESETS.default.mzLossy)
  const [intAlgo, setIntAlgo] = useState<LossyAlgo>(COMPRESSION_PRESETS.default.intLossy)
  const [zstd, setZstd] = useState([COMPRESSION_PRESETS.default.zstdLevel])
  const [extractBy, setExtractBy] = useState<ExtractMode>("mslevel")
  const [msLevel, setMsLevel] = useState("2")
  const [scanFrom, setScanFrom] = useState("1000")
  const [scanTo, setScanTo] = useState("5000")
  const [indexFrom, setIndexFrom] = useState("0")
  const [indexTo, setIndexTo] = useState("10000")
  const [outFormat, setOutFormat] = useState<"mzML" | "msz">("mzML")
  const [outputDir, setOutputDir] = useState("/data/proteomics/compressed/")

  // Convert run state.
  const [running, setRunning] = useState(false)
  const [progress, setProgress] = useState(0)
  const [phase, setPhase] = useState<string>("")
  const [result, setResult] = useState<ConvertResult | null>(null)

  // Seed the output directory from the OS default (Downloads) on mount.
  useEffect(() => {
    window.api.getDefaultOutputDir().then(setOutputDir).catch(() => {})
  }, [])

  // Subscribe to streamed progress events (proves the main->renderer channel).
  useEffect(() => {
    const off = window.api.onConvertProgress((p) => {
      setPhase(p.phase)
      console.log("[renderer] convert progress:", p.op, p.phase, p.message ?? p.error ?? "")
    })
    return off
  }, [])

  const pickOutputDir = async () => {
    const dir = await window.api.openOutputDir()
    if (dir) setOutputDir(dir)
  }

  // Choosing a preset resets the advanced controls to that preset's defaults;
  // the accordion then overrides them per-field.
  const selectPreset = (p: Preset) => {
    setProfile(p)
    const d = COMPRESSION_PRESETS[p]
    setMzAlgo(d.mzLossy)
    setIntAlgo(d.intLossy)
    setZstd([d.zstdLevel])
  }

  // Snap to a valid operation whenever the file (and thus allowed ops) changes.
  useEffect(() => {
    if (!allowed[op]) {
      const next = (["compress", "decompress", "extract"] as Op[]).find((o) => allowed[o])
      if (next) setOp(next)
    }
    setResult(null)
  }, [kind]) // eslint-disable-line react-hooks/exhaustive-deps

  // Run the selected operation through the bridge with a live (creeping) bar.
  const run = async () => {
    if (running || !allowed[op]) return
    setResult(null)
    setRunning(true)
    setProgress(8)
    const creep = setInterval(() => setProgress((p) => (p < 90 ? p + 4 : p)), 120)
    try {
      let res: ConvertResult
      if (op === "compress") {
        res = await window.api.compress(path, {
          preset: profile,
          mzLossy: mzAlgo,
          intLossy: intAlgo,
          zstdLevel: zstd[0],
          outputDir,
        })
      } else if (op === "decompress") {
        res = await window.api.decompress(path, { outputDir })
      } else {
        res = await window.api.extract(path, {
          mode: extractBy,
          msLevel: msLevel === "n" ? 3 : Number(msLevel),
          fromScan: Number(scanFrom),
          toScan: Number(scanTo),
          fromIndex: Number(indexFrom),
          toIndex: Number(indexTo),
          outputFormat: outFormat,
          outputDir,
        })
      }
      setResult(res)
    } finally {
      clearInterval(creep)
      setProgress(100)
      setRunning(false)
    }
  }

  const opItems: { value: Op; label: string; icon: typeof FileArchive; hint: string }[] = [
    { value: "compress", label: "Compress → .msz", icon: FileArchive, hint: "Only mzML files can be compressed" },
    { value: "decompress", label: "Decompress → .mzML", icon: FileDown, hint: "Only compressed files can be decompressed" },
    { value: "extract", label: "Extract", icon: Scissors, hint: "Extract a subset of spectra" },
  ]

  return (
    <div className="mx-auto max-w-3xl space-y-4 p-5">
      {/* Operation switch */}
      <Card className="gap-3 py-4">
        <CardHeader className="px-4">
          <CardTitle className="text-sm">Operation</CardTitle>
        </CardHeader>
        <CardContent className="px-4">
          <ToggleGroup
            type="single"
            value={op}
            onValueChange={(v) => v && setOp(v as Op)}
            variant="outline"
            className="w-full"
          >
            {opItems.map(({ value, label, icon: Icon, hint }) => {
              const enabled = allowed[value]
              const item = (
                <ToggleGroupItem
                  value={value}
                  disabled={!enabled}
                  className="flex-1 gap-1.5 text-xs disabled:opacity-40"
                >
                  <Icon className="size-3.5" /> {label}
                </ToggleGroupItem>
              )
              // Wrap disabled items so the tooltip explains why they're greyed out.
              return enabled ? (
                <div key={value} className="flex-1">{item}</div>
              ) : (
                <Tooltip key={value}>
                  <TooltipTrigger asChild>
                    <div className="flex-1">{item}</div>
                  </TooltipTrigger>
                  <TooltipContent side="bottom">{hint}</TooltipContent>
                </Tooltip>
              )
            })}
          </ToggleGroup>
        </CardContent>
      </Card>

      {/* Compression settings — compress only */}
      {op === "compress" && (
        <Card className="gap-3 py-4">
          <CardHeader className="px-4">
            <CardTitle className="text-sm">Compression</CardTitle>
          </CardHeader>
          <CardContent className="space-y-1 px-4">
            <div className="grid grid-cols-4 gap-2">
              {compressionProfiles.map((p) => (
                <button
                  key={p.id}
                  onClick={() => selectPreset(p.id)}
                  className={`rounded-md border p-2 text-left transition-colors ${
                    profile === p.id
                      ? "border-primary bg-primary/10"
                      : "border-border hover:bg-accent/50"
                  }`}
                >
                  <div className="text-xs font-medium">{p.label}</div>
                  <div className="mt-0.5 text-[10px] leading-tight text-muted-foreground">
                    {p.blurb}
                  </div>
                </button>
              ))}
            </div>

            <Accordion type="single" collapsible>
              <AccordionItem value="advanced" className="border-b-0">
                <AccordionTrigger className="py-2 text-xs font-medium text-muted-foreground hover:no-underline">
                  Advanced settings
                </AccordionTrigger>
                <AccordionContent className="space-y-4 pt-1">
                  <div className="grid grid-cols-2 gap-4">
                    <div className="space-y-2">
                      <SectionLabel>Lossy m/z algorithm</SectionLabel>
                      <Select value={mzAlgo} onValueChange={(v) => setMzAlgo(v as LossyAlgo)}>
                        <SelectTrigger className="w-full">
                          <SelectValue />
                        </SelectTrigger>
                        <SelectContent>
                          {lossyAlgos.map((a) => (
                            <SelectItem key={a} value={a} className="mono">
                              {a}
                            </SelectItem>
                          ))}
                        </SelectContent>
                      </Select>
                    </div>
                    <div className="space-y-2">
                      <SectionLabel>Lossy intensity algorithm</SectionLabel>
                      <Select value={intAlgo} onValueChange={(v) => setIntAlgo(v as LossyAlgo)}>
                        <SelectTrigger className="w-full">
                          <SelectValue />
                        </SelectTrigger>
                        <SelectContent>
                          {lossyAlgos.map((a) => (
                            <SelectItem key={a} value={a} className="mono">
                              {a}
                            </SelectItem>
                          ))}
                        </SelectContent>
                      </Select>
                    </div>
                  </div>

                  <div className="space-y-3">
                    <div className="flex items-center justify-between">
                      <SectionLabel>zstd level</SectionLabel>
                      <span className="mono text-xs">{zstd[0]}</span>
                    </div>
                    <Slider
                      value={zstd}
                      onValueChange={setZstd}
                      min={1}
                      max={22}
                      step={1}
                    />
                  </div>
                </AccordionContent>
              </AccordionItem>
            </Accordion>
          </CardContent>
        </Card>
      )}

      {/* Extract settings */}
      {op === "extract" && (
        <Card className="gap-3 py-4">
          <CardHeader className="px-4">
            <CardTitle className="text-sm">Extract</CardTitle>
          </CardHeader>
          <CardContent className="space-y-4 px-4">
            <div className="space-y-2">
              <SectionLabel>Extract by</SectionLabel>
              <ToggleGroup
                type="single"
                value={extractBy}
                onValueChange={(v) => v && setExtractBy(v as ExtractMode)}
                variant="outline"
                className="w-full"
              >
                <ToggleGroupItem value="mslevel" className="flex-1 text-xs">
                  MS-level
                </ToggleGroupItem>
                <ToggleGroupItem value="scan" className="flex-1 text-xs">
                  Scan range
                </ToggleGroupItem>
                <ToggleGroupItem value="index" className="flex-1 text-xs">
                  Index range
                </ToggleGroupItem>
              </ToggleGroup>
            </div>

            {extractBy === "mslevel" && (
              <div className="space-y-2">
                <SectionLabel>MS level</SectionLabel>
                <Select value={msLevel} onValueChange={setMsLevel}>
                  <SelectTrigger className="w-full">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="1">MS1</SelectItem>
                    <SelectItem value="2">MS2</SelectItem>
                    <SelectItem value="n">MSn</SelectItem>
                  </SelectContent>
                </Select>
              </div>
            )}
            {extractBy === "scan" && (
              <div className="grid grid-cols-2 gap-3">
                <div className="space-y-1.5">
                  <SectionLabel>From scan</SectionLabel>
                  <Input
                    value={scanFrom}
                    onChange={(e) => setScanFrom(e.target.value)}
                    className="mono"
                  />
                </div>
                <div className="space-y-1.5">
                  <SectionLabel>To scan</SectionLabel>
                  <Input
                    value={scanTo}
                    onChange={(e) => setScanTo(e.target.value)}
                    className="mono"
                  />
                </div>
              </div>
            )}
            {extractBy === "index" && (
              <div className="grid grid-cols-2 gap-3">
                <div className="space-y-1.5">
                  <SectionLabel>From index</SectionLabel>
                  <Input
                    value={indexFrom}
                    onChange={(e) => setIndexFrom(e.target.value)}
                    className="mono"
                  />
                </div>
                <div className="space-y-1.5">
                  <SectionLabel>To index</SectionLabel>
                  <Input
                    value={indexTo}
                    onChange={(e) => setIndexTo(e.target.value)}
                    className="mono"
                  />
                </div>
              </div>
            )}

            <div className="space-y-2">
              <SectionLabel>Output format</SectionLabel>
              <ToggleGroup
                type="single"
                value={outFormat}
                onValueChange={(v) => v && setOutFormat(v as "mzML" | "msz")}
                variant="outline"
                className="w-full"
              >
                <ToggleGroupItem value="mzML" className="mono flex-1 text-xs">
                  .mzML
                </ToggleGroupItem>
                <ToggleGroupItem value="msz" className="mono flex-1 text-xs">
                  .msz
                </ToggleGroupItem>
              </ToggleGroup>
            </div>
          </CardContent>
        </Card>
      )}

      {/* Output — local disk or remote (MSTransfer) */}
      <Card className="gap-3 py-4">
        <CardHeader className="px-4">
          <CardTitle className="text-sm">Output</CardTitle>
        </CardHeader>
        <CardContent className="px-4">
          <Tabs defaultValue="local">
            <TabsList className="grid w-full grid-cols-2">
              <TabsTrigger value="local" className="gap-1.5 text-xs">
                <HardDrive className="size-3.5" /> Local
              </TabsTrigger>
              <TabsTrigger value="remote" className="gap-1.5 text-xs">
                <Server className="size-3.5" /> Remote (MSTransfer)
              </TabsTrigger>
            </TabsList>

            <TabsContent value="local" className="mt-3 space-y-1.5">
              <SectionLabel>Output directory</SectionLabel>
              <div className="flex gap-2">
                <Input
                  readOnly
                  value={outputDir}
                  className="mono text-xs"
                />
                <Button
                  variant="secondary"
                  size="icon"
                  className="shrink-0"
                  onClick={pickOutputDir}
                >
                  <FolderOpen className="size-4" />
                </Button>
              </div>
            </TabsContent>

            <TabsContent value="remote" className="mt-3 space-y-4">
              <div className="space-y-2">
                <SectionLabel>Destination</SectionLabel>
                <Select defaultValue="storage-a">
                  <SelectTrigger className="w-full">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="storage-a">storage-a · s3://lab-archive</SelectItem>
                    <SelectItem value="hpc">hpc · cgrams@aurora:/flare</SelectItem>
                    <SelectItem value="nas">nas · smb://synology/k8</SelectItem>
                  </SelectContent>
                </Select>
              </div>
              <div className="space-y-1.5">
                <SectionLabel>Remote path</SectionLabel>
                <Input defaultValue="msz/2026/" className="mono text-xs" />
              </div>
              <div className="grid grid-cols-2 gap-4">
                <div className="space-y-2">
                  <SectionLabel>Transfer mode</SectionLabel>
                  <Select defaultValue="copy">
                    <SelectTrigger className="w-full">
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="copy">Copy</SelectItem>
                      <SelectItem value="move">Move (delete source)</SelectItem>
                    </SelectContent>
                  </Select>
                </div>
                <div className="space-y-2">
                  <SectionLabel>On conflict</SectionLabel>
                  <Select defaultValue="skip">
                    <SelectTrigger className="w-full">
                      <SelectValue />
                    </SelectTrigger>
                    <SelectContent>
                      <SelectItem value="skip">Skip existing</SelectItem>
                      <SelectItem value="overwrite">Overwrite</SelectItem>
                      <SelectItem value="rename">Keep both</SelectItem>
                    </SelectContent>
                  </Select>
                </div>
              </div>
              <p className="text-[11px] text-muted-foreground">
                Remote jobs are handed to the MSTransfer queue and run in the background.
              </p>
            </TabsContent>
          </Tabs>
        </CardContent>
      </Card>

      <Button
        size="lg"
        className="w-full gap-2"
        disabled={running || !allowed[op]}
        onClick={run}
      >
        {running ? <Loader2 className="size-4 animate-spin" /> : <Zap className="size-4" />}
        {running
          ? `Running${phase ? ` · ${phase}` : ""}…`
          : op === "compress"
            ? "Compress file"
            : op === "decompress"
              ? "Decompress file"
              : "Extract spectra"}
      </Button>

      {/* Live progress while an operation runs */}
      {running && (
        <div className="space-y-1.5">
          <Progress value={progress} className="h-1.5" />
          <div className="mono text-[11px] text-muted-foreground">
            {phase || "starting"}…
          </div>
        </div>
      )}

      {/* Completion / error state */}
      {!running && result && (
        <Card className="gap-3 py-4">
          <CardContent className="space-y-3 px-4">
            {result.error ? (
              <div className="flex items-start gap-2 text-xs text-destructive">
                <AlertTriangle className="mt-0.5 size-4 shrink-0" />
                <div>
                  <div className="font-medium">{result.op} failed</div>
                  <p className="mono mt-1 text-[11px] text-muted-foreground">
                    {result.error}
                  </p>
                </div>
              </div>
            ) : (
              <>
                <div className="flex items-center gap-2 text-xs font-medium text-emerald-400">
                  <CheckCircle2 className="size-4" /> {result.op} complete
                </div>
                <div className="grid grid-cols-3 gap-2 text-center">
                  <div className="rounded-md border border-border/60 p-2">
                    <div className="mono text-sm">{fmtBytes(result.inputBytes)}</div>
                    <div className="text-[10px] text-muted-foreground">input</div>
                  </div>
                  <div className="rounded-md border border-border/60 p-2">
                    <div className="mono text-sm">{fmtBytes(result.outputBytes)}</div>
                    <div className="text-[10px] text-muted-foreground">output</div>
                  </div>
                  <div className="rounded-md border border-border/60 p-2">
                    <div className="mono text-sm text-emerald-400">
                      {result.ratio > 0 ? `${(result.ratio * 100).toFixed(0)}%` : "—"}
                      {result.ratio > 0 && result.op === "compress" && (
                        <span className="text-muted-foreground">
                          {" "}
                          ·×{(1 / result.ratio).toFixed(1)}
                        </span>
                      )}
                    </div>
                    <div className="text-[10px] text-muted-foreground">
                      ratio · {result.elapsedMs.toFixed(0)}ms
                    </div>
                  </div>
                </div>
                <div className="flex items-center gap-2">
                  <Input readOnly value={result.outPath} className="mono text-xs" />
                  <Button
                    variant="secondary"
                    size="icon"
                    className="shrink-0"
                    title="Reveal in folder"
                    onClick={() => window.api.revealInFolder(result.outPath)}
                  >
                    <FolderSearch className="size-4" />
                  </Button>
                </div>
              </>
            )}
          </CardContent>
        </Card>
      )}
    </div>
  )
}
