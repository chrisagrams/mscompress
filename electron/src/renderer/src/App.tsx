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
} from "lucide-react"
import brandLogo from "@/assets/icon.png"
import githubMark from "@/assets/github-mark.svg"
import { Button } from "@/components/ui/button"
import { Badge } from "@/components/ui/badge"
import { Tabs, TabsList, TabsTrigger, TabsContent } from "@/components/ui/tabs"
import { TooltipProvider } from "@/components/ui/tooltip"
import { ScrollArea } from "@/components/ui/scroll-area"
import { LeftRail } from "@/components/LeftRail"
import { Inspector } from "@/components/Inspector"
import { ConvertTab } from "@/components/ConvertTab"
import { QCTab } from "@/components/QCTab"
import { ExtractQueueTab } from "@/components/ExtractQueueTab"
import { ArchiveTab } from "@/components/ArchiveTab"
import { INITIAL_FILES, type FileEntry } from "@/files"

function App() {
  const [dark, setDark] = useState(true)
  const [files, setFiles] = useState<FileEntry[]>(INITIAL_FILES)
  const [selectedPath, setSelectedPath] = useState<string>(INITIAL_FILES[0].path)
  const [tab, setTab] = useState("convert")

  const selectedEntry = files.find((f) => f.path === selectedPath) ?? files[0]

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
  const [threads, setThreads] = useState<number | null>(null)

  // Pull the real backend version + thread count from the native binding
  // through the preload bridge (replaces the old hardcoded status values).
  useEffect(() => {
    window.api
      .getVersion()
      .then((v) => {
        setBackendVersion(v)
        console.log("[renderer] backend version from window.api:", v)
      })
      .catch((e) => console.error("[renderer] getVersion failed:", e))
    window.api
      .getNumThreads()
      .then((n) => {
        setThreads(n)
        console.log("[renderer] backend threads from window.api:", n)
      })
      .catch((e) => console.error("[renderer] getNumThreads failed:", e))
  }, [])

  const toggleTheme = () => {
    const el = document.documentElement
    if (dark) {
      el.classList.remove("dark")
      el.classList.add("light")
    } else {
      el.classList.add("dark")
      el.classList.remove("light")
    }
    setDark(!dark)
  }

  const currentOp = "compress · HEK293_rep2.mzML (63%)"

  return (
    <TooltipProvider>
      <div className="flex h-screen w-screen flex-col overflow-hidden bg-background text-foreground">
        {/* Top toolbar */}
        <header className="flex h-11 shrink-0 items-center gap-3 border-b bg-card px-3">
          <div className="flex items-center gap-2">
            <img
              src={brandLogo}
              alt="MScompress"
              className="size-6 object-contain"
            />
            <span className="text-sm font-semibold tracking-tight">MScompress</span>
            <Badge variant="secondary" className="mono h-5 text-[10px]">
              workbench
            </Badge>
          </div>

          <div className="ml-auto flex items-center gap-2">
            <Button
              variant="ghost"
              size="icon"
              className="size-7"
              title="View project on GitHub"
              onClick={() =>
                window.api.openExternal("https://github.com/chrisagrams/mscompress")
              }
            >
              <img src={githubMark} alt="GitHub" className="size-4 dark:invert" />
            </Button>
            <Button
              variant="ghost"
              size="icon"
              className="size-7"
              onClick={toggleTheme}
            >
              {dark ? <Sun className="size-4" /> : <Moon className="size-4" />}
            </Button>
          </div>
        </header>

        {/* Body: three panes */}
        <div className="flex min-h-0 flex-1">
          {/* Left rail */}
          <aside className="w-64 shrink-0 border-r">
            <LeftRail
              files={files}
              selected={selectedPath}
              onSelect={setSelectedPath}
              onAddFiles={addFiles}
            />
          </aside>

          {/* Center workspace */}
          <main className="flex min-w-0 flex-1 flex-col">
            <Tabs value={tab} onValueChange={setTab} className="flex min-h-0 flex-1 flex-col gap-0">
              <div className="flex h-10 shrink-0 items-center border-b bg-card px-2">
                <TabsList className="h-8 bg-transparent p-0">
                  <TabsTrigger value="convert" className="gap-1.5 text-xs data-[state=active]:bg-accent">
                    <FileArchive className="size-3.5" /> Convert
                  </TabsTrigger>
                  <TabsTrigger value="qc" className="gap-1.5 text-xs data-[state=active]:bg-accent">
                    <Activity className="size-3.5" /> QC
                  </TabsTrigger>
                  <TabsTrigger value="extract" className="gap-1.5 text-xs data-[state=active]:bg-accent">
                    <Scissors className="size-3.5" /> Queue
                  </TabsTrigger>
                  <TabsTrigger value="archive" className="gap-1.5 text-xs data-[state=active]:bg-accent">
                    <Database className="size-3.5" /> Archive
                  </TabsTrigger>
                </TabsList>
                <span className="mono ml-auto pr-2 text-[11px] text-muted-foreground">
                  {selectedEntry.name}
                </span>
              </div>

              <ScrollArea className="min-h-0 flex-1">
                <TabsContent value="convert" className="m-0">
                  <ConvertTab kind={selectedEntry.kind} path={selectedEntry.path} />
                </TabsContent>
                <TabsContent value="qc" className="m-0">
                  <QCTab path={selectedEntry.path} name={selectedEntry.name} />
                </TabsContent>
                <TabsContent value="extract" className="m-0">
                  <ExtractQueueTab />
                </TabsContent>
                <TabsContent value="archive" className="m-0">
                  <ArchiveTab />
                </TabsContent>
              </ScrollArea>
            </Tabs>
          </main>

          {/* Right inspector */}
          <aside className="w-72 shrink-0 border-l">
            <Inspector
              path={selectedEntry.path}
              name={selectedEntry.name}
              kind={selectedEntry.kind}
            />
          </aside>
        </div>

        {/* Bottom status bar */}
        <footer className="flex h-6 shrink-0 items-center gap-4 border-t bg-primary/95 px-3 text-[11px] text-primary-foreground">
          <span className="flex items-center gap-1">
            <Activity className="size-3" /> {currentOp}
          </span>
          <div className="ml-auto flex items-center gap-4">
            <span className="mono flex items-center gap-1">
              <Cpu className="size-3" /> {threads ?? "—"} threads
            </span>
            <span className="mono flex items-center gap-1">
              <MemoryStick className="size-3" /> 3.2 / 32 GB
            </span>
            <span className="mono flex items-center gap-1">
              <Boxes className="size-3" /> mscompress {backendVersion ?? "…"}
            </span>
            <span className="mono">UTF-8</span>
          </div>
        </footer>
      </div>
    </TooltipProvider>
  )
}

export default App
