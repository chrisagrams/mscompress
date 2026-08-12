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
import { Checkbox } from "@/components/ui/checkbox"
import { Progress } from "@/components/ui/progress"
import { Separator } from "@/components/ui/separator"
import { ScrollArea } from "@/components/ui/scroll-area"
import { Tooltip, TooltipContent, TooltipTrigger } from "@/components/ui/tooltip"
import { fmtBytes } from "@/lib/format"
import type { FileKind, QueueState, QueueStatus } from "@shared/ipc"
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
}: {
  files: FileEntry[]
  /** Path of the currently selected file (drives the workspace + Inspector). */
  selected: string
  onSelect: (path: string) => void
  /** Checked files for batch actions (independent of `selected`). */
  selectedPaths: string[]
  onMultiSelect: (paths: string[]) => void
  onAddFiles: (entries: FileEntry[]) => void
  queue: QueueState
}) {
  const [dragOver, setDragOver] = useState(false)
  const [transferOpen, setTransferOpen] = useState(false)

  const running = queue.running

  // Toggle a file's checkbox in the multi-selection (batch ops consume it in
  // the Convert tab). Independent of the active row.
  const toggleChecked = (path: string) => {
    onMultiSelect(
      selectedPaths.includes(path)
        ? selectedPaths.filter((p) => p !== path)
        : [...selectedPaths, path],
    )
  }

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

      {/* Selection summary — batch actions themselves live in the Convert tab. */}
      {selectedPaths.length > 0 && (
        <div
          data-testid="selection-bar"
          className="mb-1 flex items-center gap-1 px-3 text-[11px] text-muted-foreground"
        >
          <span>{selectedPaths.length} checked · batch via Convert</span>
          <Button
            variant="ghost"
            size="icon"
            className="ml-auto size-6"
            title="Clear selection"
            onClick={() => onMultiSelect([])}
          >
            <X className="size-3.5" />
          </Button>
        </div>
      )}

      <ScrollArea className="min-h-0 flex-1">
        <div className="px-1 pb-2">
          {files.map((f) => {
            const active = f.path === selected
            const checked = selectedPaths.includes(f.path)
            return (
              <div
                key={f.path}
                data-testid="file-entry"
                data-path={f.path}
                onClick={() => onSelect(f.path)}
                className={`group flex w-full cursor-pointer items-center gap-1.5 rounded px-2 py-1 text-left text-xs transition-colors ${
                  active
                    ? "bg-accent text-accent-foreground"
                    : checked
                      ? "bg-accent/60 text-accent-foreground"
                      : "hover:bg-accent/50"
                }`}
              >
                {/* Checkbox = membership in the batch selection; clicking it
                    must not also change the active row. */}
                <Checkbox
                  data-testid="file-check"
                  className="size-3.5 shrink-0"
                  checked={checked}
                  onCheckedChange={() => toggleChecked(f.path)}
                  onClick={(e) => e.stopPropagation()}
                />
                {kindIcon(f.kind)}
                <span className="truncate">{f.name}</span>
                <span className="mono ml-auto shrink-0 text-[10px] text-muted-foreground">
                  {fmtBytes(f.size)}
                </span>
              </div>
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
