import { useEffect, useState } from "react"
import { Cpu, FolderOpen, Moon, Sun } from "lucide-react"
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogHeader,
  DialogTitle,
} from "@/components/ui/dialog"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { Slider } from "@/components/ui/slider"
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"
import { ToggleGroup, ToggleGroupItem } from "@/components/ui/toggle-group"
import { compressionProfiles } from "@/lib/profiles"
import type { AppSettings, Preset } from "@shared/ipc"

function SectionLabel({ children }: { children: React.ReactNode }) {
  return (
    <div className="text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">
      {children}
    </div>
  )
}

export function SettingsDialog({
  open,
  onOpenChange,
  settings,
}: {
  open: boolean
  onOpenChange: (v: boolean) => void
  settings: AppSettings
}) {
  const [maxThreads, setMaxThreads] = useState(16)

  useEffect(() => {
    window.api.getNumThreads().then((n) => setMaxThreads(Math.max(n, settings.threads))).catch(() => {})
  }, [settings.threads])

  const update = (partial: Partial<AppSettings>) => {
    void window.api.setSettings(partial)
  }

  const pickOutputDir = async () => {
    const dir = await window.api.openOutputDir()
    if (dir) update({ defaultOutputDir: dir })
  }

  return (
    <Dialog open={open} onOpenChange={onOpenChange}>
      <DialogContent className="sm:max-w-lg">
        <DialogHeader>
          <DialogTitle>Settings</DialogTitle>
          <DialogDescription>
            Persisted to disk and applied across the app.
          </DialogDescription>
        </DialogHeader>

        <div className="space-y-5 py-2">
          {/* Threads */}
          <div className="space-y-3">
            <div className="flex items-center justify-between">
              <SectionLabel>
                <span className="flex items-center gap-1.5">
                  <Cpu className="size-3" /> Worker threads
                </span>
              </SectionLabel>
              <span className="mono text-xs">
                {settings.threads} / {maxThreads}
              </span>
            </div>
            <Slider
              value={[Math.min(settings.threads, maxThreads)]}
              onValueChange={(v) => update({ threads: v[0] })}
              min={1}
              max={maxThreads}
              step={1}
            />
            <p className="text-[11px] text-muted-foreground">
              Used for all compress / decompress / extract jobs (no per-job override).
            </p>
          </div>

          {/* Default output directory */}
          <div className="space-y-2">
            <SectionLabel>Default output directory</SectionLabel>
            <div className="flex gap-2">
              <Input readOnly value={settings.defaultOutputDir} className="mono text-xs" />
              <Button variant="secondary" size="icon" className="shrink-0" onClick={pickOutputDir}>
                <FolderOpen className="size-4" />
              </Button>
            </div>
          </div>

          {/* Default preset */}
          <div className="space-y-2">
            <SectionLabel>Default compression preset</SectionLabel>
            <Select
              value={settings.defaultPreset}
              onValueChange={(v) => update({ defaultPreset: v as Preset })}
            >
              <SelectTrigger className="w-full">
                <SelectValue />
              </SelectTrigger>
              <SelectContent>
                {compressionProfiles.map((p) => (
                  <SelectItem key={p.id} value={p.id}>
                    {p.label} — {p.blurb}
                  </SelectItem>
                ))}
              </SelectContent>
            </Select>
          </div>

          {/* Theme */}
          <div className="space-y-2">
            <SectionLabel>Theme</SectionLabel>
            <ToggleGroup
              type="single"
              value={settings.theme}
              onValueChange={(v) => v && update({ theme: v as "dark" | "light" })}
              variant="outline"
              className="w-full"
            >
              <ToggleGroupItem value="dark" className="flex-1 gap-1.5 text-xs">
                <Moon className="size-3.5" /> Dark
              </ToggleGroupItem>
              <ToggleGroupItem value="light" className="flex-1 gap-1.5 text-xs">
                <Sun className="size-3.5" /> Light
              </ToggleGroupItem>
            </ToggleGroup>
          </div>
        </div>
      </DialogContent>
    </Dialog>
  )
}
