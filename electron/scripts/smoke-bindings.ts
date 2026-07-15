// Main-process binding smoke test. Imports the REAL wrappers from
// src/main/bindings.ts (the same functions the IPC handlers call) and exercises
// them against native mscompress + real test data. Run: node scripts/smoke-bindings.ts
import { fileURLToPath } from 'node:url'
import { dirname, resolve } from 'node:path'
import { getVersion, getNumThreads, getFilesize, analyze } from '../src/main/bindings.ts'

const here = dirname(fileURLToPath(import.meta.url))
const dataDir = resolve(here, '../../test/data')

console.log('getVersion()      =', JSON.stringify(getVersion()))
console.log('getNumThreads()   =', getNumThreads())

const mzml = resolve(dataDir, 'test.mzML')
console.log('getFilesize(mzML) =', getFilesize(mzml))
console.log('analyze(mzML)     =', JSON.stringify(analyze(mzml)))

const msz = resolve(dataDir, 'test.msz')
console.log('analyze(msz)      =', JSON.stringify(analyze(msz)))
