import { Database, FileText, Check, X } from "lucide-react"
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
import { sampleMszxManifest } from "@/mockData"

function Field({ label, value }: { label: string; value: React.ReactNode }) {
  return (
    <div className="flex items-baseline justify-between gap-4 py-1.5">
      <span className="text-[11px] uppercase tracking-wider text-muted-foreground">
        {label}
      </span>
      <span className="mono text-right text-xs">{value}</span>
    </div>
  )
}

export function ArchiveTab() {
  const m = sampleMszxManifest
  return (
    <div className="space-y-3 p-4">
      <div className="flex items-center gap-2">
        <Database className="size-4 text-chart-2" />
        <h2 className="text-sm font-semibold">MSZX Archive</h2>
        <Badge variant="secondary" className="mono">v{m.version}</Badge>
      </div>

      <div className="grid grid-cols-2 gap-3">
        <Card className="gap-2 py-3">
          <CardHeader className="px-4">
            <CardTitle className="text-xs font-semibold uppercase tracking-wider text-muted-foreground">
              Manifest
            </CardTitle>
          </CardHeader>
          <CardContent className="px-4">
            <Field label="Source file" value={m.source_file} />
            <Separator />
            <Field label="Spectra file" value={m.spectra_file} />
            <Separator />
            <Field label="Num spectra" value={m.num_spectra.toLocaleString()} />
            <Separator />
            <Field label="Join key" value={m.join_key} />
            <Separator />
            <Field label="Created" value={new Date(m.created_at).toLocaleString()} />
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
              {m.description}
            </p>
            <div className="mt-3 flex flex-wrap gap-1.5">
              <Badge variant="outline" className="mono">Orbitrap</Badge>
              <Badge variant="outline" className="mono">DDA</Badge>
              <Badge variant="outline" className="mono">tryptic</Badge>
            </div>
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
          <Table>
            <TableHeader>
              <TableRow className="hover:bg-transparent">
                <TableHead className="text-[11px] uppercase tracking-wider">Filename</TableHead>
                <TableHead className="text-[11px] uppercase tracking-wider">Format</TableHead>
                <TableHead className="text-[11px] uppercase tracking-wider">Compressed</TableHead>
                <TableHead className="text-right text-[11px] uppercase tracking-wider">Records</TableHead>
                <TableHead className="text-[11px] uppercase tracking-wider">Description</TableHead>
              </TableRow>
            </TableHeader>
            <TableBody>
              {m.annotations.map((a) => (
                <TableRow key={a.filename}>
                  <TableCell className="mono text-xs font-medium">{a.filename}</TableCell>
                  <TableCell>
                    <Badge variant="secondary" className="mono text-[10px]">{a.format}</Badge>
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
                  <TableCell className="mono text-right text-xs">{a.num_records.toLocaleString()}</TableCell>
                  <TableCell className="text-xs text-muted-foreground">{a.description}</TableCell>
                </TableRow>
              ))}
            </TableBody>
          </Table>
        </CardContent>
      </Card>
    </div>
  )
}
