// Main-process binding smoke test. Imports the REAL wrappers from
// src/main/bindings.ts (the same functions the IPC handlers call) and exercises
// them against native mscompress + real test data. Run: node scripts/smoke-bindings.ts
import { fileURLToPath } from 'node:url'
import { dirname, resolve, join } from 'node:path'
import { mkdtempSync, rmSync, existsSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { createRequire } from 'node:module'
import { getVersion, getNumThreads, analyze, compress, decompress, extract } from '../src/main/bindings.ts'

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
