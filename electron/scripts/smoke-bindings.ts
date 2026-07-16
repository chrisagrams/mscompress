// Main-process binding smoke test. Imports the REAL wrappers from
// src/main/bindings.ts (the same functions the IPC handlers call) and exercises
// them against native mscompress + real test data. Run: node scripts/smoke-bindings.ts
import { fileURLToPath } from 'node:url'
import { dirname, resolve } from 'node:path'
import { getVersion, getNumThreads, analyze } from '../src/main/bindings.ts'

const here = dirname(fileURLToPath(import.meta.url))
const dataDir = resolve(here, '../../test/data')

console.log('getVersion()    =', JSON.stringify(getVersion()))
console.log('getNumThreads() =', getNumThreads())
console.log('')

for (const rel of ['test.mzML', 'test.msz', 'mszx/test.mszx', 'corrupt_base64.mzML']) {
  const summary = analyze(resolve(dataDir, rel))
  console.log(`analyze(${rel}) =`)
  console.log(JSON.stringify(summary, null, 2))
  console.log('')
}
