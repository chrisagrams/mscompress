import { useEffect, useState } from "react"
import {
  Boxes,
  Sun,
  Moon,
  Cpu,
  MemoryStick,
  FileArchive,
  Activity,
  Database,
  Scissors,
  Settings as SettingsIcon,
} from "lucide-react"
import brandLogo from "@/assets/icon.png"
import githubMark from "@/assets/github-mark.svg"
import { Button } from "@/components/ui/button"
import { Tabs, TabsList, TabsTrigger, TabsContent } from "@/components/ui/tabs"
import { TooltipProvider } from "@/components/ui/tooltip"
import { ScrollArea } from "@/components/ui/scroll-area"
import { LeftRail } from "@/components/LeftRail"
import { Inspector } from "@/components/Inspector"
import { ConvertTab } from "@/components/ConvertTab"
import { QCTab } from "@/components/QCTab"
import { ExtractQueueTab } from "@/components/ExtractQueueTab"
import { ArchiveTab } from "@/components/ArchiveTab"
import { SettingsDialog } from "@/components/SettingsDialog"
import { type FileEntry } from "@/files"
import {
  TITLE_BAR_OVERLAY,
  type AppSettings,
  type QueueState,
  type SystemMemory,
} from "@shared/ipc"

const GB = 1024 ** 3

/** Placeholder shown in the workspace/inspector when no file is open. */
function NoFile() {
  return (
    <div
      data-testid="empty-state"
      className="flex h-full min-h-40 flex-col items-center justify-center gap-2 p-8 text-center text-muted-foreground"
    >
      <FileArchive className="size-8 opacity-40" />
      <p className="text-sm">No file open</p>
      <p className="text-xs">Open or drop an mzML / msz / mszx file to get started.</p>
    </div>
  )
}

function App() {
  const [files, setFiles] = useState<FileEntry[]>([])
  const [selectedPath, setSelectedPath] = useState<string>("")
  const [tab, setTab] = useState("convert")
  const [settings, setSettings] = useState<AppSettings | null>(null)
  const [settingsOpen, setSettingsOpen] = useState(false)

  // undefined when no files are open (empty state).
  const selectedEntry = files.find((f) => f.path === selectedPath) ?? files[0]

  // Live MSTransfer queue state (single subscription shared by the rail + tab).
  const [queue, setQueue] = useState<QueueState>({
    jobs: [],
    running: 0,
    paused: true,
    maxConcurrency: 1,
  })
  useEffect(() => {
    window.api
      .getQueueState()
      .then(setQueue)
      .catch(() => {})
    return window.api.onQueueUpdate(setQueue)
  }, [])

  // Add opened/dropped files (deduped by path) and select the first new one.
  const addFiles = (entries: FileEntry[]) => {
    if (entries.length === 0) return
    setFiles((prev) => {
      const seen = new Set(prev.map((f) => f.path))
      const fresh = entries.filter((e) => !seen.has(e.path))
      return [...fresh, ...prev]
    })
    setSelectedPath(entries[0].path)
  }
  const [backendVersion, setBackendVersion] = useState<string | null>(null)

  // Backend version for the status bar.
  useEffect(() => {
    window.api
      .getVersion()
      .then(setBackendVersion)
      .catch(() => {})
  }, [])

  // Live memory readout for the status bar — polled from the main process.
  const [mem, setMem] = useState<SystemMemory | null>(null)
  useEffect(() => {
    let alive = true
    const poll = (): void => {
      window.api
        .getMemory()
        .then((m) => alive && setMem(m))
        .catch(() => {})
    }
    poll()
    const id = setInterval(poll, 2500)
    return () => {
      alive = false
      clearInterval(id)
    }
  }, [])

  // Track the currently running direct convert (compress/decompress/extract) via
  // the real streamed progress events — set while working, cleared when done.
  const [activeOp, setActiveOp] = useState<{ op: string; file: string } | null>(null)
  useEffect(() => {
    return window.api.onConvertProgress((p) => {
      setActiveOp(p.phase === "start" || p.phase === "step" ? { op: p.op, file: p.file } : null)
    })
  }, [])

  // Load + subscribe to persisted settings; apply the theme app-wide.
  useEffect(() => {
    window.api
      .getSettings()
      .then(setSettings)
      .catch(() => {})
    return window.api.onSettingsChange(setSettings)
  }, [])
  useEffect(() => {
    if (!settings) return
    const el = document.documentElement
    if (settings.theme === "light") {
      el.classList.add("light")
      el.classList.remove("dark")
    } else {
      el.classList.add("dark")
      el.classList.remove("light")
    }
    // Keep the Windows native window-control overlay in sync with the theme.
    // Safe no-op on macOS/Linux (guarded in the main process).
    void window.api.setTitleBarOverlay(TITLE_BAR_OVERLAY[settings.theme]).catch(() => {})
  }, [settings?.theme])

  const dark = settings ? settings.theme === "dark" : true
  const toggleTheme = () => {
    if (settings) void window.api.setSettings({ theme: dark ? "light" : "dark" })
  }

  // Real current-operation label: a direct convert takes precedence, else a
  // running queue job (with its real progress %), else idle.
  const runningJob = queue.jobs.find((j) => j.status === "running")
  const currentOp = activeOp
    ? `${activeOp.op} · ${activeOp.file}`
    : runningJob
      ? `${runningJob.op} · ${runningJob.fileName} (${runningJob.progress}%)`
      : "Ready"

  const memLabel = mem
    ? `${(mem.rssBytes / GB).toFixed(1)} / ${(mem.totalBytes / GB).toFixed(0)} GB`
    : "…"

  return (
    <TooltipProvider>
      <div className="flex h-screen w-screen flex-col overflow-hidden bg-background text-foreground">
        {/* Top toolbar — drag region for the frameless window (app-toolbar gets
            the traffic-light inset on darwin and the Window Controls Overlay
            inset on win32; interactive controls opt out of dragging via
            no-drag). */}
        <header className="app-toolbar drag-region flex h-11 shrink-0 items-center gap-3 bg-card px-3">
          <div className="flex items-center gap-2">
            <img src={brandLogo} alt="MScompress" className="size-6 object-contain" />
            <span className="text-sm font-semibold tracking-tight">MScompress</span>
          </div>

          <div className="no-drag ml-auto flex items-center gap-2">
            <Button
              variant="ghost"
              size="icon"
              className="size-7"
              title="View project on GitHub"
              onClick={() => window.api.openExternal("https://github.com/chrisagrams/mscompress")}
            >
              <img src={githubMark} alt="GitHub" className="size-4 dark:invert" />
            </Button>
            <Button
              variant="ghost"
              size="icon"
              className="size-7"
              title="Settings"
              onClick={() => setSettingsOpen(true)}
            >
              <SettingsIcon className="size-4" />
            </Button>
            <Button variant="ghost" size="icon" className="size-7" onClick={toggleTheme}>
              {dark ? <Sun className="size-4" /> : <Moon className="size-4" />}
            </Button>
          </div>
        </header>

        {/* Full-width toolbar separator, rendered BELOW the header so it spans the
            entire width — including under the Windows native window controls,
            which the Window Controls Overlay paints over the header's own top-right
            corner and would otherwise clip a header `border-b`. */}
        <div className="h-px shrink-0 bg-border" />

        {settings && (
          <SettingsDialog open={settingsOpen} onOpenChange={setSettingsOpen} settings={settings} />
        )}

        {/* Body: three panes */}
        <div className="flex min-h-0 flex-1">
          {/* Left rail */}
          <aside className="w-64 shrink-0 border-r">
            <LeftRail
              files={files}
              selected={selectedPath}
              onSelect={setSelectedPath}
              onAddFiles={addFiles}
              queue={queue}
            />
          </aside>

          {/* Center workspace */}
          <main className="flex min-w-0 flex-1 flex-col">
            <Tabs value={tab} onValueChange={setTab} className="flex min-h-0 flex-1 flex-col gap-0">
              <div className="flex h-10 shrink-0 items-center border-b bg-card px-2">
                <TabsList className="h-8 bg-transparent p-0">
                  <TabsTrigger
                    value="convert"
                    className="gap-1.5 text-xs data-[state=active]:bg-accent"
                  >
                    <FileArchive className="size-3.5" /> Convert
                  </TabsTrigger>
                  <TabsTrigger value="qc" className="gap-1.5 text-xs data-[state=active]:bg-accent">
                    <Activity className="size-3.5" /> QC
                  </TabsTrigger>
                  <TabsTrigger
                    value="extract"
                    className="gap-1.5 text-xs data-[state=active]:bg-accent"
                  >
                    <Scissors className="size-3.5" /> Queue
                  </TabsTrigger>
                  <TabsTrigger
                    value="archive"
                    className="gap-1.5 text-xs data-[state=active]:bg-accent"
                  >
                    <Database className="size-3.5" /> Archive
                  </TabsTrigger>
                </TabsList>
                <span className="mono ml-auto pr-2 text-[11px] text-muted-foreground">
                  {selectedEntry?.name ?? "No file open"}
                </span>
              </div>

              <ScrollArea className="min-h-0 flex-1">
                <TabsContent value="convert" className="m-0">
                  {selectedEntry ? (
                    <ConvertTab
                      kind={selectedEntry.kind}
                      path={selectedEntry.path}
                      settings={settings}
                    />
                  ) : (
                    <NoFile />
                  )}
                </TabsContent>
                <TabsContent value="qc" className="m-0">
                  {selectedEntry ? (
                    <QCTab path={selectedEntry.path} name={selectedEntry.name} />
                  ) : (
                    <NoFile />
                  )}
                </TabsContent>
                <TabsContent value="extract" className="m-0">
                  <ExtractQueueTab queue={queue} />
                </TabsContent>
                <TabsContent value="archive" className="m-0">
                  {selectedEntry ? (
                    <ArchiveTab
                      path={selectedEntry.path}
                      kind={selectedEntry.kind}
                      name={selectedEntry.name}
                    />
                  ) : (
                    <NoFile />
                  )}
                </TabsContent>
              </ScrollArea>
            </Tabs>
          </main>

          {/* Right inspector */}
          <aside className="w-72 shrink-0 border-l">
            {selectedEntry ? (
              <Inspector
                path={selectedEntry.path}
                name={selectedEntry.name}
                kind={selectedEntry.kind}
              />
            ) : (
              <NoFile />
            )}
          </aside>
        </div>

        {/* Bottom status bar */}
        <footer className="flex h-6 shrink-0 items-center gap-4 border-t bg-muted px-3 text-[11px] text-muted-foreground">
          <span className="flex items-center gap-1">
            <Activity className="size-3" /> {currentOp}
          </span>
          <div className="ml-auto flex items-center gap-4">
            <span className="mono flex items-center gap-1">
              <Cpu className="size-3" /> {settings?.threads ?? "…"} threads
            </span>
            <span className="mono flex items-center gap-1">
              <MemoryStick className="size-3" /> {memLabel}
            </span>
            <span className="mono flex items-center gap-1">
              <Boxes className="size-3" /> mscompress {backendVersion ?? "…"}
            </span>
          </div>
        </footer>
      </div>
    </TooltipProvider>
  )
}

export default App
