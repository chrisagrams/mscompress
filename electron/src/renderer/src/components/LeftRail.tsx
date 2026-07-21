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
  X,
} from "lucide-react"
import { Button } from "@/components/ui/button"
import { Badge } from "@/components/ui/badge"
import { Progress } from "@/components/ui/progress"
import { Separator } from "@/components/ui/separator"
import { ScrollArea } from "@/components/ui/scroll-area"
import { Tooltip, TooltipContent, TooltipTrigger } from "@/components/ui/tooltip"
import { fmtBytes } from "@/lib/format"
import { COMPRESSION_PRESETS } from "@shared/ipc"
import type { AppSettings, FileKind, QueueState, QueueStatus } from "@shared/ipc"
import { ingestPaths, type FileEntry } from "@/files"

// File-kind colours match the OS file-type icons (assets/icons/*): the logo's
// own brand hues — msz=blue (logo "s"), mzML=magenta (logo "m"), mszx=green.
function kindIcon(kind: FileKind) {
  if (kind === "msz") return <FileArchive className="size-3.5 text-[#24a8df]" />
  if (kind === "mszx") return <Database className="size-3.5 text-[#10b981]" />
  return <FileIcon className="size-3.5 text-[#e60f8a]" />
}

function statusBadge(status: QueueStatus) {
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
  files,
  selected,
  onSelect,
  selectedPaths,
  onMultiSelect,
  onAddFiles,
  queue,
  settings,
}: {
  files: FileEntry[]
  /** Path of the currently selected file (drives the workspace + Inspector). */
  selected: string
  onSelect: (path: string) => void
  /** Multi-selection for batch actions (independent of `selected`). */
  selectedPaths: string[]
  onMultiSelect: (paths: string[]) => void
  onAddFiles: (entries: FileEntry[]) => void
  queue: QueueState
  settings: AppSettings | null
}) {
  const [dragOver, setDragOver] = useState(false)
  const [transferOpen, setTransferOpen] = useState(false)
  // Anchor row for shift-range selection (index into `files`).
  const [anchor, setAnchor] = useState<number | null>(null)

  const running = queue.running

  // Click on a file row: plain → single-select (resets multi to just this file);
  // Ctrl/Cmd → toggle in the multi-selection; Shift → range from the anchor row.
  const handleRowClick = (e: React.MouseEvent, path: string, idx: number) => {
    if (e.shiftKey && anchor !== null) {
      const [a, b] = anchor <= idx ? [anchor, idx] : [idx, anchor]
      onMultiSelect(files.slice(a, b + 1).map((f) => f.path))
    } else if (e.ctrlKey || e.metaKey) {
      onMultiSelect(
        selectedPaths.includes(path)
          ? selectedPaths.filter((p) => p !== path)
          : [...selectedPaths, path],
      )
      setAnchor(idx)
    } else {
      onSelect(path)
      onMultiSelect([path])
      setAnchor(idx)
    }
  }

  // Enqueue one LOCAL compress job per selected mzML, then start the queue.
  const compressSelected = async () => {
    const s = settings ?? (await window.api.getSettings())
    const preset = s.defaultPreset ?? "default"
    const d = COMPRESSION_PRESETS[preset]
    const targets = files.filter((f) => selectedPaths.includes(f.path) && f.kind === "mzML")
    if (targets.length === 0) return
    for (const f of targets) {
      await window.api.addQueueJob({
        filePath: f.path,
        kind: "mzML",
        op: "compress",
        settings: {
          preset,
          mzLossy: d.mzLossy,
          intLossy: d.intLossy,
          zstdLevel: d.zstdLevel,
          outputDir: s.defaultOutputDir,
        },
      })
    }
    await window.api.startQueue()
    setTransferOpen(true)
    onMultiSelect([])
  }

  const compressCount = files.filter(
    (f) => selectedPaths.includes(f.path) && f.kind === "mzML",
  ).length

  // Analyze paths via the shared helper and add the resulting entries.
  const ingest = async (paths: string[]) => {
    const entries = await ingestPaths(paths)
    onAddFiles(entries)
  }

  // Open the native file dialog and add the picked files.
  const handleOpenFiles = async () => {
    const paths = await window.api.openFiles()
    console.log("[renderer] openFiles ->", paths)
    await ingest(paths)
  }

  // Real files dropped onto the zone: resolve their absolute paths, then analyze.
  const handleDrop = async (dropped: FileList) => {
    const paths = Array.from(dropped).map((f) => window.api.getPathForFile(f))
    console.log("[renderer] dropped ->", paths)
    await ingest(paths)
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
              data-testid="open-files"
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

      {/* Batch action bar — shown when 2+ files are multi-selected and at least
          one is an mzML (the only kind that can be compressed). */}
      {selectedPaths.length >= 2 && compressCount > 0 && (
        <div
          data-testid="batch-bar"
          className="mb-1 flex items-center gap-1 px-2 text-[11px] text-muted-foreground"
        >
          <span>{selectedPaths.length} selected</span>
          <Button
            size="sm"
            data-testid="compress-selected"
            className="ml-auto h-6 gap-1 text-[11px]"
            onClick={compressSelected}
          >
            <FileArchive className="size-3" /> Compress selected
          </Button>
          <Button
            variant="ghost"
            size="icon"
            className="size-6"
            title="Clear selection"
            onClick={() => onMultiSelect([])}
          >
            <X className="size-3.5" />
          </Button>
        </div>
      )}

      <ScrollArea className="min-h-0 flex-1">
        <div className="px-1 pb-2">
          {files.map((f, idx) => {
            const active = f.path === selected
            const multi = selectedPaths.includes(f.path)
            return (
              <button
                key={f.path}
                data-testid="file-entry"
                data-path={f.path}
                onClick={(e) => handleRowClick(e, f.path, idx)}
                className={`group flex w-full items-center gap-1.5 rounded px-2 py-1 text-left text-xs transition-colors ${
                  active
                    ? "bg-accent text-accent-foreground"
                    : multi
                      ? "bg-accent/60 text-accent-foreground"
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

      {/* Queue / MSTransfer — collapsible, hidden by default, pinned to bottom */}
      <div className="mt-auto flex min-h-0 flex-col">
        <Separator />
        <button
          type="button"
          onClick={() => setTransferOpen((o) => !o)}
          className="flex w-full items-center justify-between px-3 py-2 text-left"
        >
          <div className="flex items-center gap-1.5 text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">
            <ChevronRight
              className={`size-3 shrink-0 transition-transform ${transferOpen ? "rotate-90" : ""}`}
            />
            MSTransfer Queue
            <Badge variant="secondary" className="mono h-4 px-1.5 text-[10px]">
              {running} run
            </Badge>
          </div>
        </button>
        {transferOpen && (
          <>
            <div className="flex items-center gap-1 px-2 pb-2">
              <Button
                variant="secondary"
                size="sm"
                className="h-6 flex-1 gap-1 text-[11px]"
                onClick={() => window.api.startQueue()}
              >
                <Play className="size-3" /> Start
              </Button>
              <Button
                variant="ghost"
                size="sm"
                className="h-6 flex-1 gap-1 text-[11px]"
                onClick={() => window.api.pauseQueue()}
              >
                <Pause className="size-3" /> Pause
              </Button>
              <Button
                variant="ghost"
                size="icon"
                className="size-6"
                title="Clear queue"
                onClick={() => window.api.clearQueue()}
              >
                <Trash2 className="size-3.5" />
              </Button>
            </div>

            <ScrollArea className="max-h-[45vh] min-h-0">
              <div className="space-y-1 px-2 pb-3">
                {queue.jobs.length === 0 && (
                  <div className="px-2 py-3 text-center text-[11px] text-muted-foreground">
                    No jobs. Send one from Convert → Remote.
                  </div>
                )}
                {queue.jobs.map((q) => (
                  <div key={q.id} className="rounded-md border border-border/60 bg-card/40 p-2">
                    <div className="flex items-center gap-1.5">
                      {kindIcon(q.kind)}
                      <span className="truncate text-xs">{q.fileName}</span>
                    </div>
                    <div className="mt-1 flex items-center justify-between">
                      {statusBadge(q.status)}
                      <span className="mono text-[10px] text-muted-foreground">{q.op}</span>
                    </div>
                    {(q.status === "running" || q.status === "queued") && (
                      <Progress value={q.progress} className="mt-1.5 h-1" />
                    )}
                    <div className="mono mt-1 flex items-center justify-between text-[10px] text-muted-foreground">
                      <span>{fmtBytes(q.sizeBytes)}</span>
                      {q.status === "done" && q.ratio != null && (
                        <span className="text-emerald-400">
                          {(q.ratio * 100).toFixed(0)}% · ×{(1 / q.ratio).toFixed(1)}
                        </span>
                      )}
                      {q.status === "error" && <span className="text-destructive">error</span>}
                    </div>
                  </div>
                ))}
              </div>
            </ScrollArea>
          </>
        )}
      </div>
    </div>
  )
}
