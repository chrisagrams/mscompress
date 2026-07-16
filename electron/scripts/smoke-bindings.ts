// Main-process binding smoke test. Imports the REAL wrappers from
// src/main/bindings.ts (the same functions the IPC handlers call) and exercises
// them against native mscompress + real test data. Run: node scripts/smoke-bindings.ts
import { fileURLToPath } from 'node:url'
import { dirname, resolve, join, basename } from 'node:path'
import { mkdtempSync, rmSync, existsSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { createRequire } from 'node:module'
import {
  getVersion,
  getNumThreads,
  analyze,
  computeQC,
  compress,
  decompress,
  extract
} from '../src/main/bindings.ts'
import { queue, configureQueue } from '../src/main/queue.ts'
import type { QueueState } from '../src/shared/ipc.ts'
import { readdirSync } from 'node:fs'

const require = createRequire(import.meta.url)
const { read } = require('mscompress')

const here = dirname(fileURLToPath(import.meta.url))
const dataDir = resolve(here, '../../test/data')

console.log('getVersion()    =', JSON.stringify(getVersion()))
console.log('getNumThreads() =', getNumThreads())
console.log('')

// ---- analyze (T3) ----
for (const rel of ['test.mzML', 'test.msz', 'mszx/test.mszx', 'corrupt_base64.mzML']) {
  const s = analyze(resolve(dataDir, rel))
  console.log(`analyze(${rel}): kind=${s.kind} spectra=${s.spectrumCount} err=${s.error ?? '-'}`)
}
console.log('')

// ---- QC (T5) ----
for (const rel of ['test.mzML', 'sciex_ttof6600_100.mzML']) {
  const q = computeQC(resolve(dataDir, rel))
  const nonzero = q.heatmap.cells.length
  console.log(`computeQC(${rel}):`)
  console.log(`  spectrumCount=${q.spectrumCount} sampledCount=${q.sampledCount} err=${q.error ?? '-'}`)
  console.log(
    `  tic: len=${q.tic.length} first=${JSON.stringify(q.tic[0])} last=${JSON.stringify(q.tic[q.tic.length - 1])}`
  )
  console.log(`  bpc: len=${q.bpc.length}`)
  console.log(
    `  heatmap: ${q.heatmap.rtBins}x${q.heatmap.mzBins} mz[${q.heatmap.mzMin.toFixed(0)}..${q.heatmap.mzMax.toFixed(0)}] rtMax=${q.heatmap.rtMax.toFixed(3)} nonzeroCells=${nonzero}`
  )
  console.log(`  msLevelCounts=${JSON.stringify(q.msLevelCounts)}`)
  console.log(`  peaksPerSpectrum=${JSON.stringify(q.peaksPerSpectrum)}`)
}
console.log('')

// ---- convert (T4): compress -> decompress -> extract ----
const out = mkdtempSync(join(tmpdir(), 'msc-t4-'))
try {
  const src = resolve(dataDir, 'test.mzML')
  const srcSpectra = (() => {
    const f = read(src)
    const n = f.spectra.length
    f.close()
    return n
  })()

  const c = compress(src, { preset: 'default', outputDir: out })
  console.log('COMPRESS  ', JSON.stringify(c))
  console.log('  outPath exists:', existsSync(c.outPath), '| outputBytes>0:', c.outputBytes > 0)

  const d = decompress(c.outPath, { outputDir: out })
  const dSpectra = (() => {
    const f = read(d.outPath)
    const n = f.spectra.length
    f.close()
    return n
  })()
  console.log('DECOMPRESS', JSON.stringify(d))
  console.log('  outPath exists:', existsSync(d.outPath), '| re-read spectra:', dSpectra)

  const e = extract(src, { mode: 'mslevel', msLevel: 2, outputFormat: 'mzML', outputDir: out })
  const eSpectra = (() => {
    const f = read(e.outPath)
    const n = f.spectra.length
    f.close()
    return n
  })()
  console.log('EXTRACT   ', JSON.stringify(e))
  console.log(`  source spectra: ${srcSpectra} | extracted MS2 spectra: ${eSpectra} | fewer: ${eSpectra < srcSpectra}`)
} finally {
  rmSync(out, { recursive: true, force: true })
  console.log('\ncleaned temp dir', out)
}

// ---- MSTransfer queue (T6) ----
console.log('\n=== queue ===')
{
  const qdir = mkdtempSync(join(tmpdir(), 'msc-q-'))
  const archive = mkdtempSync(join(tmpdir(), 'msc-archive-'))
  configureQueue({ localArchiveDir: archive, maxConcurrency: 1 })
  const src = resolve(dataDir, 'test.mzML')

  const idOk = queue.add({
    filePath: src,
    kind: 'mzML',
    op: 'compress',
    settings: { preset: 'default', outputDir: qdir }
  })
  const idBad = queue.add({
    filePath: resolve(dataDir, 'does-not-exist.mzML'),
    kind: 'mzML',
    op: 'compress',
    settings: { preset: 'fast', outputDir: qdir }
  })
  const idRemote = queue.add({
    filePath: src,
    kind: 'mzML',
    op: 'compress',
    settings: { preset: 'fast' },
    destinationId: 'local-archive',
    remotePath: 'msz/2026'
  })

  const done = (s: QueueState) => s.running === 0 && s.jobs.every((j) => j.status === 'done' || j.status === 'error')
  await new Promise<void>((resolve2) => {
    const off = queue.onUpdate((s) => {
      if (done(s)) {
        off()
        resolve2()
      }
    })
    queue.start()
  })

  const state = queue.state()
  for (const j of state.jobs) {
    console.log(
      `  job ${j.id} [${j.op}] ${j.fileName} -> status=${j.status} progress=${j.progress}` +
        (j.status === 'done' ? ` ratio=${j.ratio?.toFixed(3)} out=${basename(j.outPath ?? '')}` : '') +
        (j.finalPath ? ` finalPath=${j.finalPath}` : '') +
        (j.error ? ` error="${j.error}"` : '')
    )
  }
  const okJob = state.jobs.find((j) => j.id === idOk)!
  const badJob = state.jobs.find((j) => j.id === idBad)!
  const remoteJob = state.jobs.find((j) => j.id === idRemote)!
  console.log(`  OK job done + output exists: ${okJob.status === 'done' && !!okJob.outPath && existsSync(okJob.outPath)}`)
  console.log(`  BAD job errored (no crash): ${badJob.status === 'error'} error="${badJob.error}"`)
  console.log(`  REMOTE job landed in archive: ${remoteJob.status === 'done' && !!remoteJob.finalPath && existsSync(remoteJob.finalPath ?? '')}`)
  console.log('  archive dir contents:', JSON.stringify(readdirSync(join(archive, 'msz', '2026'))))

  rmSync(qdir, { recursive: true, force: true })
  rmSync(archive, { recursive: true, force: true })
  console.log('  cleaned queue temp dirs')
}
