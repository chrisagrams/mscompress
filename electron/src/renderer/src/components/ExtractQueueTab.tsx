import { Play, Pause, Trash2, Check, Loader2, AlertCircle, Clock } from "lucide-react"
import { Button } from "@/components/ui/button"
import { Badge } from "@/components/ui/badge"
import { Progress } from "@/components/ui/progress"
import { Card } from "@/components/ui/card"
import {
  Table,
  TableBody,
  TableCell,
  TableHead,
  TableHeader,
  TableRow,
} from "@/components/ui/table"
import { sampleQueue, fmtBytes, type QueueItem } from "@/mockData"

function StatusPill({ status }: { status: QueueItem["status"] }) {
  const map = {
    done: { icon: Check, cls: "border-emerald-500/40 text-emerald-400" },
    running: { icon: Loader2, cls: "border-sky-500/40 text-sky-400" },
    error: { icon: AlertCircle, cls: "border-destructive/50 text-destructive" },
    queued: { icon: Clock, cls: "text-muted-foreground" },
  } as const
  const { icon: Icon, cls } = map[status]
  return (
    <Badge variant="outline" className={`gap-1 ${cls}`}>
      <Icon className={`size-3 ${status === "running" ? "animate-spin" : ""}`} />
      {status}
    </Badge>
  )
}

export function ExtractQueueTab() {
  const total = sampleQueue.reduce((a, b) => a + b.sizeBytes, 0)
  const done = sampleQueue.filter((q) => q.status === "done").length

  return (
    <div className="space-y-3 p-4">
      <div className="flex items-center justify-between">
        <div className="flex items-center gap-3">
          <h2 className="text-sm font-semibold">MSTransfer — Batch Queue</h2>
          <Badge variant="secondary" className="mono">
            {done}/{sampleQueue.length} done
          </Badge>
          <span className="mono text-xs text-muted-foreground">
            {fmtBytes(total)} total
          </span>
        </div>
        <div className="flex items-center gap-2">
          <Button size="sm" className="gap-1.5">
            <Play className="size-3.5" /> Start all
          </Button>
          <Button size="sm" variant="secondary" className="gap-1.5">
            <Pause className="size-3.5" /> Pause
          </Button>
          <Button size="sm" variant="ghost" className="gap-1.5">
            <Trash2 className="size-3.5" /> Clear
          </Button>
        </div>
      </div>

      <Card className="overflow-hidden py-0">
        <Table>
          <TableHeader>
            <TableRow className="hover:bg-transparent">
              <TableHead className="text-[11px] uppercase tracking-wider">File</TableHead>
              <TableHead className="text-[11px] uppercase tracking-wider">Op</TableHead>
              <TableHead className="text-[11px] uppercase tracking-wider">Status</TableHead>
              <TableHead className="w-[220px] text-[11px] uppercase tracking-wider">Progress</TableHead>
              <TableHead className="text-right text-[11px] uppercase tracking-wider">Size</TableHead>
              <TableHead className="text-right text-[11px] uppercase tracking-wider">Ratio / ETA</TableHead>
            </TableRow>
          </TableHeader>
          <TableBody>
            {sampleQueue.map((q) => (
              <TableRow key={q.id}>
                <TableCell className="font-medium">{q.fileName}</TableCell>
                <TableCell className="mono text-xs text-muted-foreground">{q.op}</TableCell>
                <TableCell><StatusPill status={q.status} /></TableCell>
                <TableCell>
                  <div className="flex items-center gap-2">
                    <Progress value={q.progress} className="h-1.5" />
                    <span className="mono w-9 text-right text-[11px] text-muted-foreground">
                      {q.progress}%
                    </span>
                  </div>
                </TableCell>
                <TableCell className="mono text-right text-xs">{fmtBytes(q.sizeBytes)}</TableCell>
                <TableCell className="mono text-right text-xs">
                  {q.status === "done" && q.ratio != null && (
                    <span className="text-emerald-400">
                      {(q.ratio * 100).toFixed(0)}% · ×{(1 / q.ratio).toFixed(1)}
                    </span>
                  )}
                  {q.status === "running" && q.etaSec != null && (
                    <span className="text-muted-foreground">{q.etaSec}s</span>
                  )}
                  {(q.status === "queued" || q.status === "error") && (
                    <span className="text-muted-foreground">—</span>
                  )}
                </TableCell>
              </TableRow>
            ))}
          </TableBody>
        </Table>
      </Card>
    </div>
  )
}
