// MSTransfer batch queue: an in-memory job queue that runs T4 convert jobs and
// optionally transfers outputs to a remote destination. Electron-free so it can
// be exercised directly from a Node smoke test; the main process wires it to
// IPC + configures the local archive dir.
import { copyFileSync, mkdirSync, rmSync, statSync } from 'fs'
import { basename, join } from 'path'
import { REMOTE_DESTINATIONS } from '../shared/ipc.ts'
import type {
  CompressOptions,
  ConvertResult,
  DecompressOptions,
  ExtractOptions,
  QueueAddRequest,
  QueueJob,
  QueueState
} from '../shared/ipc.ts'
import * as bindings from './bindings.ts'

type Listener = (state: QueueState) => void

interface JobInternal {
  job: QueueJob
  settings: CompressOptions | DecompressOptions | ExtractOptions
  remotePath?: string
  transferMode: 'copy' | 'move'
}

interface QueueConfig {
  /** Base directory for the `local-archive` remote destination + staging. */
  localArchiveDir: string
  maxConcurrency: number
  /** Worker threads from global settings (0 → let the binding decide). */
  threads: number
}

const config: QueueConfig = { localArchiveDir: '', maxConcurrency: 1, threads: 0 }

/** Configure runtime bits the queue can't derive on its own (electron paths). */
export function configureQueue(opts: Partial<QueueConfig>): void {
  if (opts.localArchiveDir !== undefined) config.localArchiveDir = opts.localArchiveDir
  if (opts.maxConcurrency !== undefined) config.maxConcurrency = Math.max(1, opts.maxConcurrency)
  if (opts.threads !== undefined) config.threads = Math.max(0, opts.threads)
}

class JobQueue {
  private items: JobInternal[] = []
  private runningIds = new Set<string>()
  private paused = true
  private seq = 0
  private listeners = new Set<Listener>()

  onUpdate(cb: Listener): () => void {
    this.listeners.add(cb)
    return () => this.listeners.delete(cb)
  }

  state(): QueueState {
    return {
      jobs: this.items.map((it) => ({ ...it.job })),
      running: this.runningIds.size,
      paused: this.paused,
      maxConcurrency: config.maxConcurrency
    }
  }

  private emit(): void {
    const s = this.state()
    for (const l of this.listeners) l(s)
  }

  private safeSize(path: string): number {
    try {
      return statSync(path).size
    } catch {
      return 0
    }
  }

  add(req: QueueAddRequest): string {
    const id = `job${++this.seq}`
    const dest = req.destinationId
      ? REMOTE_DESTINATIONS.find((d) => d.id === req.destinationId)
      : undefined
    const job: QueueJob = {
      id,
      filePath: req.filePath,
      fileName: req.fileName ?? basename(req.filePath),
      kind: req.kind,
      op: req.op,
      status: 'queued',
      progress: 0,
      sizeBytes: this.safeSize(req.filePath),
      destinationId: dest?.id,
      destinationLabel: dest?.label
    }
    this.items.push({
      job,
      settings: req.settings,
      remotePath: req.remotePath,
      transferMode: req.transferMode ?? 'copy'
    })
    this.emit()
    this.pump()
    return id
  }

  start(): void {
    this.paused = false
    this.emit()
    this.pump()
  }

  pause(): void {
    this.paused = true
    this.emit()
  }

  clear(): void {
    this.items = this.items.filter((it) => this.runningIds.has(it.job.id))
    this.emit()
  }

  remove(id: string): void {
    if (this.runningIds.has(id)) return
    this.items = this.items.filter((it) => it.job.id !== id)
    this.emit()
  }

  private pump(): void {
    if (this.paused) return
    while (this.runningIds.size < config.maxConcurrency) {
      const next = this.items.find((it) => it.job.status === 'queued')
      if (!next) break
      void this.runJob(next)
    }
  }

  private execConvert(it: JobInternal, outputDir: string): ConvertResult {
    const { op } = it.job
    // Threads come from global settings, not per-job (0 → binding default).
    const threads = config.threads > 0 ? config.threads : undefined
    if (op === 'compress') {
      return bindings.compress(it.job.filePath, {
        ...(it.settings as CompressOptions),
        outputDir,
        threads
      })
    }
    if (op === 'decompress') {
      return bindings.decompress(it.job.filePath, {
        ...(it.settings as DecompressOptions),
        outputDir,
        threads
      })
    }
    return bindings.extract(it.job.filePath, {
      ...(it.settings as ExtractOptions),
      outputDir,
      threads
    })
  }

  /** Transfer a finished output to its remote destination. */
  private transferRemote(it: JobInternal, outPath: string): { finalPath?: string; error?: string } {
    const dest = REMOTE_DESTINATIONS.find((d) => d.id === it.job.destinationId)
    if (!dest) return { error: `Unknown destination: ${it.job.destinationId}` }
    if (dest.type !== 'local' || !dest.configured) {
      // Honest stub: validate + refuse, never fake a successful upload.
      return { error: `Remote destination '${dest.label}' (${dest.type}) is not configured` }
    }
    if (!config.localArchiveDir) {
      return { error: 'Local archive directory is not configured' }
    }
    const targetDir = join(config.localArchiveDir, it.remotePath ?? '')
    mkdirSync(targetDir, { recursive: true })
    const target = join(targetDir, basename(outPath))
    copyFileSync(outPath, target)
    if (it.transferMode === 'move') rmSync(outPath, { force: true })
    return { finalPath: target }
  }

  private async runJob(it: JobInternal): Promise<void> {
    const { job } = it
    job.status = 'running'
    job.progress = 10
    this.runningIds.add(job.id)
    this.emit()
    // Yield so the "running" state flushes before the blocking native call.
    await new Promise((r) => setImmediate(r))

    try {
      // Remote jobs convert into a staging dir, then transfer; local jobs use
      // the outputDir supplied in their settings.
      const staged = job.destinationId
      const outputDir = staged
        ? join(config.localArchiveDir || '.', '.staging')
        : ((it.settings as { outputDir?: string }).outputDir ?? '')
      if (staged) mkdirSync(outputDir, { recursive: true })

      job.progress = 45
      this.emit()
      await new Promise((r) => setImmediate(r))

      const result = this.execConvert(it, outputDir)
      if (result.error) {
        job.status = 'error'
        job.error = result.error
        job.progress = 100
        return
      }
      job.outPath = result.outPath
      job.ratio = result.ratio
      job.progress = job.destinationId ? 75 : 100

      if (job.destinationId) {
        this.emit()
        const transfer = this.transferRemote(it, result.outPath)
        if (transfer.error) {
          job.status = 'error'
          job.error = transfer.error
          if (it.transferMode !== 'move') job.finalPath = undefined
        } else {
          job.finalPath = transfer.finalPath
          job.status = 'done'
        }
        job.progress = 100
      } else {
        job.status = 'done'
      }
    } catch (err) {
      job.status = 'error'
      job.error = err instanceof Error ? err.message : String(err)
      job.progress = 100
    } finally {
      this.runningIds.delete(job.id)
      this.emit()
      this.pump()
    }
  }
}

export const queue = new JobQueue()
