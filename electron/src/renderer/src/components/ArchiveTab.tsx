import { useEffect, useState } from "react"
import { Database, FileText, Check, X, Loader2, AlertTriangle, Package } from "lucide-react"
import { Badge } from "@/components/ui/badge"
import { Card, CardContent, CardHeader, CardTitle } from "@/components/ui/card"
import { Separator } from "@/components/ui/separator"
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table"
import { fmtBytes } from "@/lib/format"
import type { FileKind, MszxArchive, MszxBatchManifest, MszxManifest } from "@shared/ipc"

function Field({ label, value }: { label: string; value: React.ReactNode }) {
  return (
    <div className="flex items-baseline justify-between gap-4 py-1.5">
      <span className="text-[11px] uppercase tracking-wider text-muted-foreground">{label}</span>
      <span className="mono text-right text-xs">{value}</span>
    </div>
  )
}

function fmtCreated(iso: string): string {
  const d = new Date(iso)
  return Number.isNaN(d.getTime()) ? iso || "—" : d.toLocaleString()
}

export function ArchiveTab({ path, kind, name }: { path: string; kind: FileKind; name: string }) {
  const [manifest, setManifest] = useState<MszxArchive | null>(null)
  const [loading, setLoading] = useState(false)

  useEffect(() => {
    if (kind !== "mszx") {
      setManifest(null)
      setLoading(false)
      return
    }
    let active = true
    setLoading(true)
    setManifest(null)
    window.api
      .readMszx(path)
      .then((m) => {
        if (!active) return
        setManifest(m)
        console.log("[renderer] archive:", m.path, "container=", m.container)
      })
      .catch((e) => {
        if (active) setManifest({ error: String(e) } as MszxManifest)
      })
      .finally(() => {
        if (active) setLoading(false)
      })
    return () => {
      active = false
    }
  }, [path, kind])

  // Not an archive → prompt state.
  if (kind !== "mszx") {
    return (
      <div className="flex h-[60vh] flex-col items-center justify-center gap-2 p-6 text-center text-muted-foreground">
        <Package className="size-7" />
        <span className="text-sm font-medium">No archive selected</span>
        <p className="max-w-sm text-xs">
          <span className="mono">{name}</span> is a {kind} file. Select an{" "}
          <span className="mono">.mszx</span> archive in the Explorer to inspect its manifest and
          annotations.
        </p>
      </div>
    )
  }

  if (loading) {
    return (
      <div className="flex h-[60vh] flex-col items-center justify-center gap-2 text-muted-foreground">
        <Loader2 className="size-6 animate-spin" />
        <span className="text-xs">Reading {name}…</span>
      </div>
    )
  }

  if (!manifest || manifest.error) {
    return (
      <div className="flex h-[60vh] flex-col items-center justify-center gap-2 p-6 text-center text-destructive">
        <AlertTriangle className="size-6" />
        <span className="text-sm font-medium">Could not read archive</span>
        <p className="mono max-w-md text-[11px] text-muted-foreground">
          {manifest?.error ?? "No data"}
        </p>
      </div>
    )
  }

  if (manifest.container === "batch") {
    return <BatchArchiveView m={manifest} name={name} />
  }

  const m = manifest
  return (
    <div data-testid="archive-manifest" className="space-y-3 p-4">
      <div className="flex items-center gap-2">
        <Database className="size-4 text-chart-2" />
        <h2 className="text-sm font-semibold">MSZX Archive</h2>
        <Badge variant="secondary" className="mono">
          v{m.version}
        </Badge>
        <span className="mono ml-auto text-[11px] text-muted-foreground">{name}</span>
      </div>

      <div className="grid grid-cols-2 gap-3">
        <Card className="gap-2 py-3">
          <CardHeader className="px-4">
            <CardTitle className="text-xs font-semibold uppercase tracking-wider text-muted-foreground">
              Manifest
            </CardTitle>
          </CardHeader>
          <CardContent className="px-4">
            <Field label="Source file" value={m.source_file ?? "—"} />
            <Separator />
            <Field label="Spectra file" value={m.spectra_file || "—"} />
            <Separator />
            <Field label="Num spectra" value={m.num_spectra.toLocaleString()} />
            <Separator />
            <Field label="Join key" value={m.join_key || "—"} />
            <Separator />
            <Field label="Created" value={fmtCreated(m.created_at)} />
          </CardContent>
        </Card>

        <Card className="gap-2 py-3">
          <CardHeader className="px-4">
            <CardTitle className="text-xs font-semibold uppercase tracking-wider text-muted-foreground">
              Description
            </CardTitle>
          </CardHeader>
          <CardContent className="px-4">
            <p className="text-sm leading-relaxed text-muted-foreground">
              {m.description ?? "No description in manifest."}
            </p>
          </CardContent>
        </Card>
      </div>

      <Card className="gap-2 py-3">
        <CardHeader className="px-4">
          <CardTitle className="flex items-center gap-2 text-xs font-semibold uppercase tracking-wider text-muted-foreground">
            <FileText className="size-3.5" /> Annotations ({m.annotations.length})
          </CardTitle>
        </CardHeader>
        <CardContent className="px-0">
          {m.annotations.length === 0 ? (
            <p className="px-4 py-2 text-xs text-muted-foreground">
              This archive has no annotation files.
            </p>
          ) : (
            <Table>
              <TableHeader>
                <TableRow className="hover:bg-transparent">
                  <TableHead className="text-[11px] uppercase tracking-wider">Filename</TableHead>
                  <TableHead className="text-[11px] uppercase tracking-wider">Format</TableHead>
                  <TableHead className="text-[11px] uppercase tracking-wider">Compressed</TableHead>
                  <TableHead className="text-right text-[11px] uppercase tracking-wider">
                    Records
                  </TableHead>
                  <TableHead className="text-[11px] uppercase tracking-wider">
                    Description
                  </TableHead>
                </TableRow>
              </TableHeader>
              <TableBody>
                {m.annotations.map((a) => (
                  <TableRow key={a.filename}>
                    <TableCell className="mono text-xs font-medium">{a.filename}</TableCell>
                    <TableCell>
                      <Badge variant="secondary" className="mono text-[10px]">
                        {a.format}
                      </Badge>
                    </TableCell>
                    <TableCell>
                      {a.compressed ? (
                        <span className="flex items-center gap-1 text-xs text-emerald-400">
                          <Check className="size-3.5" /> yes
                        </span>
                      ) : (
                        <span className="flex items-center gap-1 text-xs text-muted-foreground">
                          <X className="size-3.5" /> no
                        </span>
                      )}
                    </TableCell>
                    <TableCell className="mono text-right text-xs">
                      {a.num_records != null ? a.num_records.toLocaleString() : "—"}
                    </TableCell>
                    <TableCell className="text-xs text-muted-foreground">
                      {a.description ?? "—"}
                    </TableCell>
                  </TableRow>
                ))}
              </TableBody>
            </Table>
          )}
        </CardContent>
      </Card>
    </div>
  )
}

/** v2 multi-file ("batch") archive: summary + one row per bundled MSZ. */
function BatchArchiveView({ m, name }: { m: MszxBatchManifest; name: string }) {
  const knownSpectra = m.entries.reduce((sum, e) => sum + (e.num_spectra ?? 0), 0)
  const totalBytes = m.entries.reduce((sum, e) => sum + e.size, 0)
  const annotationCount = m.entries.reduce((sum, e) => sum + e.annotations.length, 0)

  return (
    <div data-testid="archive-batch" className="space-y-3 p-4">
      <div className="flex items-center gap-2">
        <Database className="size-4 text-chart-2" />
        <h2 className="text-sm font-semibold">MSZX Batch Archive</h2>
        <Badge variant="secondary" className="mono">
          v{m.version}
        </Badge>
        <span className="mono ml-auto text-[11px] text-muted-foreground">{name}</span>
      </div>

      <div className="grid grid-cols-2 gap-3">
        <Card className="gap-2 py-3">
          <CardHeader className="px-4">
            <CardTitle className="text-xs font-semibold uppercase tracking-wider text-muted-foreground">
              Summary
            </CardTitle>
          </CardHeader>
          <CardContent className="px-4">
            <Field label="Entries" value={m.entries.length.toLocaleString()} />
            <Separator />
            <Field
              label="Total spectra"
              value={knownSpectra > 0 ? knownSpectra.toLocaleString() : "—"}
            />
            <Separator />
            <Field label="Payload size" value={totalBytes > 0 ? fmtBytes(totalBytes) : "—"} />
            <Separator />
            <Field label="Annotations" value={annotationCount.toLocaleString()} />
          </CardContent>
        </Card>

        <Card className="gap-2 py-3">
          <CardHeader className="px-4">
            <CardTitle className="text-xs font-semibold uppercase tracking-wider text-muted-foreground">
              Description
            </CardTitle>
          </CardHeader>
          <CardContent className="px-4">
            <p className="text-sm leading-relaxed text-muted-foreground">
              {m.description ?? "No description in manifest."}
            </p>
          </CardContent>
        </Card>
      </div>

      <Card className="gap-2 py-3">
        <CardHeader className="px-4">
          <CardTitle className="flex items-center gap-2 text-xs font-semibold uppercase tracking-wider text-muted-foreground">
            <FileText className="size-3.5" /> Entries ({m.entries.length})
          </CardTitle>
        </CardHeader>
        <CardContent className="px-0">
          <Table>
            <TableHeader>
              <TableRow className="hover:bg-transparent">
                <TableHead className="text-[11px] uppercase tracking-wider">Entry</TableHead>
                <TableHead className="text-[11px] uppercase tracking-wider">Original</TableHead>
                <TableHead className="text-right text-[11px] uppercase tracking-wider">
                  Size
                </TableHead>
                <TableHead className="text-right text-[11px] uppercase tracking-wider">
                  Spectra
                </TableHead>
                <TableHead className="text-right text-[11px] uppercase tracking-wider">
                  Annotations
                </TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {m.entries.map((e) => (
                <TableRow key={e.entry}>
                  <TableCell className="mono text-xs font-medium">{e.entry}</TableCell>
                  <TableCell className="mono text-xs text-muted-foreground">
                    {e.original ?? "—"}
                  </TableCell>
                  <TableCell className="mono text-right text-xs">
                    {e.size > 0 ? fmtBytes(e.size) : "—"}
                  </TableCell>
                  <TableCell className="mono text-right text-xs">
                    {e.num_spectra != null ? e.num_spectra.toLocaleString() : "—"}
                  </TableCell>
                  <TableCell className="mono text-right text-xs">
                    {e.annotations.length > 0 ? e.annotations.length : "—"}
                  </TableCell>
                </TableRow>
              ))}
            </TableBody>
          </Table>
        </CardContent>
      </Card>
    </div>
  )
}
