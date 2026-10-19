import { useState } from "react"
import { Files, FolderOpen, Upload, File as FileIcon, FileArchive, Database, X } from "lucide-react"
import { Button } from "@/components/ui/button"
import { Checkbox } from "@/components/ui/checkbox"
import { ScrollArea } from "@/components/ui/scroll-area"
import { Tooltip, TooltipContent, TooltipTrigger } from "@/components/ui/tooltip"
import { fmtBytes } from "@/lib/format"
import type { FileKind } from "@shared/ipc"
import { ingestPaths, type FileEntry } from "@/files"

// File-kind colours match the OS file-type icons (assets/icons/*): the logo's
// own brand hues — msz=blue (logo "s"), mzML=magenta (logo "m"), mszx=green.
function kindIcon(kind: FileKind) {
  if (kind === "msz") return <FileArchive className="size-3.5 text-[#24a8df]" />
  if (kind === "mszx") return <Database className="size-3.5 text-[#10b981]" />
  return <FileIcon className="size-3.5 text-[#e60f8a]" />
}

export function LeftRail({
  files,
  selected,
  onSelect,
  selectedPaths,
  onMultiSelect,
  onAddFiles,
}: {
  files: FileEntry[]
  /** Path of the currently selected file (drives the workspace + Inspector). */
  selected: string
  onSelect: (path: string) => void
  /** Checked files for batch actions (independent of `selected`). */
  selectedPaths: string[]
  onMultiSelect: (paths: string[]) => void
  onAddFiles: (entries: FileEntry[]) => void
}) {
  const [dragOver, setDragOver] = useState(false)

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
    </div>
  )
}
