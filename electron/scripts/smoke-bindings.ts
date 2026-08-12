// Main-process binding smoke test. Imports the REAL wrappers from
// src/main/bindings.ts (the same functions the IPC handlers call) and exercises
// them against native mscompress + real test data. Run: node scripts/smoke-bindings.ts
import { fileURLToPath } from 'node:url'
import { dirname, resolve, join, basename } from 'node:path'
import { mkdtempSync, rmSync, existsSync, writeFileSync, readFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { createRequire } from 'node:module'
import {
  getVersion,
  getNumThreads,
  analyze,
  computeQC,
  readMszx,
  compress,
  compressBatch,
  decompress,
  extract
} from '../src/main/bindings.ts'
import { queue, configureQueue } from '../src/main/queue.ts'
import {
  configureSettings,
  loadSettings,
  getSettings,
  setSettings
} from '../src/main/settings.ts'
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

// ---- MSZX archive (T7) ----
{
  const m = readMszx(resolve(dataDir, 'mszx/test.mszx'))
  if (m.container !== 'single') throw new Error('expected test.mszx to be a v1 single-file archive')
  console.log('readMszx(test.mszx):')
  console.log(
    `  container=${m.container} version=${m.version} num_spectra=${m.num_spectra} join_key=${m.join_key} source_file=${m.source_file} err=${m.error ?? '-'}`
  )
  console.log(`  description=${JSON.stringify(m.description)}`)
  console.log(`  annotations(${m.annotations.length})=${JSON.stringify(m.annotations)}`)

  const bad = readMszx(resolve(dataDir, 'mszx/does-not-exist.mszx'))
  console.log(`readMszx(bad path): error="${bad.error}" (container=${bad.container})`)
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

// ---- Batch mszx (v2): compressBatch -> readMszx -> decompress ----
console.log('\n=== batch mszx ===')
{
  const bdir = mkdtempSync(join(tmpdir(), 'msc-batch-'))
  try {
    const inputs = [resolve(dataDir, 'test.mzML'), resolve(dataDir, 'sciex_ttof6600_100.mzML')]
    const archive = join(bdir, 'cohort.mszx')
    const progress: string[] = []
    const b = await compressBatch(inputs, archive, { preset: 'default' }, (i, total, p) =>
      progress.push(`${i + 1}/${total} ${basename(p)}`)
    )
    console.log('COMPRESS-BATCH', JSON.stringify(b))
    console.log('  progress events:', JSON.stringify(progress))
    console.log(
      `  archive exists: ${existsSync(archive)} | inputCount=${b.inputCount} | err=${b.error ?? '-'}`
    )

    const m = readMszx(archive)
    if (m.container !== 'batch') throw new Error('expected a batch (v2) manifest')
    console.log(`  readMszx: container=${m.container} version=${m.version} entries=${m.entries.length}`)
    for (const e of m.entries) {
      console.log(`    ${e.entry} original=${e.original} size=${e.size} spectra=${e.num_spectra}`)
    }

    const d = decompress(archive, { outputDir: bdir })
    console.log(
      'DECOMPRESS-BATCH',
      JSON.stringify({ ...d, outPaths: d.outPaths?.map((p) => basename(p)) })
    )
    const allExist = d.outPaths?.every((p) => existsSync(p)) ?? false
    console.log(`  outDir exists: ${existsSync(d.outPath)} | files=${d.outPaths?.length} | all exist: ${allExist}`)
    const reread = (d.outPaths ?? []).map((p) => {
      const f = read(p)
      const n = f.spectra.length
      f.close()
      return n
    })
    console.log('  re-read spectra per file:', JSON.stringify(reread))

    // Per-member QC on the batch archive.
    const q = computeQC(archive, { entry: m.entries[0].entry })
    console.log(
      `  computeQC(entry=${m.entries[0].entry}): spectra=${q.spectrumCount} sampled=${q.sampledCount} tic=${q.tic.length} err=${q.error ?? '-'}`
    )
    const qNoEntry = computeQC(archive)
    console.log(`  computeQC without entry rejected: error="${qNoEntry.error}"`)

    // Error path: batch compress must reject a non-mzML input structurally.
    const badBatch = await compressBatch([resolve(dataDir, 'test.msz')], join(bdir, 'bad.mszx'), {
      preset: 'default'
    })
    console.log(`  non-mzML input rejected: error="${badBatch.error}"`)
  } finally {
    rmSync(bdir, { recursive: true, force: true })
    console.log('  cleaned batch temp dir')
  }
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

// ---- Global settings (T8) ----
console.log('\n=== settings ===')
{
  const sdir = mkdtempSync(join(tmpdir(), 'msc-settings-'))
  const sfile = join(sdir, 'settings.json')
  configureSettings({ dir: sdir, defaultThreads: 8, defaultOutputDir: '/tmp/out' })

  const created = loadSettings()
  console.log('  defaults created:', JSON.stringify(created))
  console.log('  settings.json exists:', existsSync(sfile))

  setSettings({ threads: 4, defaultPreset: 'better' })

  // Reload from disk in a fresh module state to prove it persisted.
  configureSettings({ dir: sdir, defaultThreads: 8, defaultOutputDir: '/tmp/out' })
  const reloaded = loadSettings()
  console.log('  reloaded from disk:', JSON.stringify(reloaded))
  console.log('  on-disk file:', readFileSync(sfile, 'utf-8').replace(/\s+/g, ' '))
  console.log(
    `  round-trip OK: ${reloaded.threads === 4 && reloaded.defaultPreset === 'better'}`
  )

  // Corrupt the file → loader must fall back to defaults without crashing.
  writeFileSync(sfile, '{ this is not: valid json ]]', 'utf-8')
  configureSettings({ dir: sdir, defaultThreads: 8, defaultOutputDir: '/tmp/out' })
  const recovered = loadSettings()
  console.log('  after corruption, recovered:', JSON.stringify(recovered))
  console.log(
    `  corrupt fallback OK (no crash, defaults): ${recovered.threads === 8 && recovered.defaultPreset === 'default'}`
  )
  void getSettings

  rmSync(sdir, { recursive: true, force: true })
  console.log('  cleaned settings temp dir')
}
