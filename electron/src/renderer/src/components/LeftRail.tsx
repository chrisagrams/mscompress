import { useState } from "react"
import {
  Files,
  FolderOpen,
  Upload,
  File as FileIcon,
  FileArchive,
  Database,
  ChevronRight,
  Play,
  Pause,
  Trash2,
  Loader2,
  Check,
  AlertCircle,
  Clock,
} from "lucide-react"
import { Button } from "@/components/ui/button"
import { Badge } from "@/components/ui/badge"
import { Progress } from "@/components/ui/progress"
import { Separator } from "@/components/ui/separator"
import { ScrollArea } from "@/components/ui/scroll-area"
import {
  Tooltip,
  TooltipContent,
  TooltipTrigger,
} from "@/components/ui/tooltip"
import { sampleQueue, fmtBytes, type QueueItem, type FileKind } from "@/mockData"

type OpenFile = { name: string; kind: FileKind; size: number }

const initialFiles: OpenFile[] = [
  { name: "HEK293_rep1.mzML", kind: "mzML", size: 2_411_724_800 },
  { name: "HEK293_rep2.mzML", kind: "mzML", size: 2_388_100_000 },
  { name: "Plasma_DIA_01.mzML", kind: "mzML", size: 5_120_400_000 },
  { name: "QC_pool_A.mzML", kind: "mzML", size: 1_002_000_000 },
  { name: "archive_2023.msz", kind: "msz", size: 640_200_000 },
  { name: "HEK293_rep1.mszx", kind: "mszx", size: 671_000_000 },
]

function kindIcon(kind: FileKind) {
  if (kind === "msz") return <FileArchive className="size-3.5 text-chart-4" />
  if (kind === "mszx") return <Database className="size-3.5 text-chart-2" />
  return <FileIcon className="size-3.5 text-chart-1" />
}

function statusBadge(status: QueueItem["status"]) {
  switch (status) {
    case "done":
      return (
        <Badge variant="outline" className="gap-1 border-emerald-500/40 text-emerald-400">
          <Check className="size-3" /> done
        </Badge>
      )
    case "running":
      return (
        <Badge variant="outline" className="gap-1 border-sky-500/40 text-sky-400">
          <Loader2 className="size-3 animate-spin" /> running
        </Badge>
      )
    case "error":
      return (
        <Badge variant="outline" className="gap-1 border-destructive/50 text-destructive">
          <AlertCircle className="size-3" /> error
        </Badge>
      )
    default:
      return (
        <Badge variant="outline" className="gap-1 text-muted-foreground">
          <Clock className="size-3" /> queued
        </Badge>
      )
  }
}

export function LeftRail({
  selected,
  onSelect,
}: {
  selected: string
  onSelect: (name: string) => void
}) {
  const [dragOver, setDragOver] = useState(false)
  const [added, setAdded] = useState<OpenFile[]>([])
  const openFiles = [...added, ...initialFiles]

  const running = sampleQueue.filter((q) => q.status === "running").length

  // Open the native file dialog, analyze each pick through the binding, and
  // add them to the Explorer list.
  const handleOpenFiles = async () => {
    const paths = await window.api.openFiles()
    console.log("[renderer] openFiles ->", paths)
    const summaries = await Promise.all(paths.map((p) => window.api.analyze(p)))
    summaries.forEach((s) => console.log("[renderer] analyzed:", s))
    setAdded((prev) => [
      ...summaries.map((s) => ({
        name: s.fileName,
        kind: s.kind,
        size: s.filesizeBytes,
      })),
      ...prev,
    ])
    if (summaries[0]) onSelect(summaries[0].fileName)
  }

  // Real files dropped onto the zone: resolve their paths and analyze them.
  const handleDrop = async (files: FileList) => {
    for (const f of Array.from(files)) {
      const path = window.api.getPathForFile(f)
      const summary = await window.api.analyze(path)
      console.log("[renderer] dropped + analyzed:", summary)
      setAdded((prev) => [
        { name: summary.fileName, kind: summary.kind, size: summary.filesizeBytes },
        ...prev,
      ])
    }
  }

  return (
    <div className="flex h-full flex-col bg-sidebar text-sidebar-foreground">
      {/* Explorer header */}
      <div className="flex items-center justify-between px-3 py-2">
        <div className="flex items-center gap-1.5 text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">
          <Files className="size-3.5" /> Explorer
        </div>
        <Tooltip>
          <TooltipTrigger asChild>
            <Button
              variant="ghost"
              size="icon"
              className="size-6"
              onClick={handleOpenFiles}
            >
              <FolderOpen className="size-3.5" />
            </Button>
          </TooltipTrigger>
          <TooltipContent side="bottom">Open file dialog…</TooltipContent>
        </Tooltip>
      </div>

      {/* Drop zone */}
      <div
        className={`mx-2 mb-2 rounded-md border border-dashed px-3 py-3 text-center text-[11px] transition-colors ${
          dragOver
            ? "border-primary bg-primary/10 text-foreground"
            : "border-border text-muted-foreground"
        }`}
        onDragOver={(e) => {
          e.preventDefault()
          setDragOver(true)
        }}
        onDragLeave={() => setDragOver(false)}
        onDrop={(e) => {
          e.preventDefault()
          setDragOver(false)
          if (e.dataTransfer.files.length) void handleDrop(e.dataTransfer.files)
        }}
      >
        <Upload className="mx-auto mb-1 size-4" />
        Drop .mzML / .msz here
      </div>

      {/* Open files */}
      <div className="px-3 pb-1 text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">
        Open Files
      </div>
      <ScrollArea className="min-h-0 flex-[0_0_auto] max-h-[40%]">
        <div className="px-1 pb-2">
          {openFiles.map((f) => {
            const active = f.name === selected
            return (
              <button
                key={f.name}
                onClick={() => onSelect(f.name)}
                className={`group flex w-full items-center gap-1.5 rounded px-2 py-1 text-left text-xs transition-colors ${
                  active
                    ? "bg-accent text-accent-foreground"
                    : "hover:bg-accent/50"
                }`}
              >
                <ChevronRight
                  className={`size-3 shrink-0 text-muted-foreground transition-transform ${
                    active ? "rotate-90" : ""
                  }`}
                />
                {kindIcon(f.kind)}
                <span className="truncate">{f.name}</span>
                <span className="mono ml-auto shrink-0 text-[10px] text-muted-foreground">
                  {fmtBytes(f.size)}
                </span>
              </button>
            )
          })}
        </div>
      </ScrollArea>

      <Separator />

      {/* Queue / MSTransfer */}
      <div className="flex items-center justify-between px-3 py-2">
        <div className="flex items-center gap-1.5 text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">
          MSTransfer Queue
          <Badge variant="secondary" className="mono h-4 px-1.5 text-[10px]">
            {running} run
          </Badge>
        </div>
      </div>
      <div className="flex items-center gap-1 px-2 pb-2">
        <Button variant="secondary" size="sm" className="h-6 flex-1 gap-1 text-[11px]">
          <Play className="size-3" /> Start
        </Button>
        <Button variant="ghost" size="sm" className="h-6 flex-1 gap-1 text-[11px]">
          <Pause className="size-3" /> Pause
        </Button>
        <Button variant="ghost" size="icon" className="size-6">
          <Trash2 className="size-3.5" />
        </Button>
      </div>

      <ScrollArea className="min-h-0 flex-1">
        <div className="space-y-1 px-2 pb-3">
          {sampleQueue.map((q) => (
            <div
              key={q.id}
              className="rounded-md border border-border/60 bg-card/40 p-2"
            >
              <div className="flex items-center gap-1.5">
                {kindIcon(q.kind)}
                <span className="truncate text-xs">{q.fileName}</span>
              </div>
              <div className="mt-1 flex items-center justify-between">
                {statusBadge(q.status)}
                <span className="mono text-[10px] text-muted-foreground">
                  {q.op}
                </span>
              </div>
              {q.status === "running" && (
                <Progress value={q.progress} className="mt-1.5 h-1" />
              )}
              <div className="mono mt-1 flex items-center justify-between text-[10px] text-muted-foreground">
                <span>{fmtBytes(q.sizeBytes)}</span>
                {q.status === "done" && q.ratio != null && (
                  <span className="text-emerald-400">
                    {(q.ratio * 100).toFixed(0)}% · ×{(1 / q.ratio).toFixed(1)}
                  </span>
                )}
                {q.status === "running" && q.etaSec != null && (
                  <span>ETA {q.etaSec}s</span>
                )}
              </div>
            </div>
          ))}
        </div>
      </ScrollArea>
    </div>
  )
}
