// Persisted global settings. Electron-free (so it's smoke-testable under Node):
// the main process injects the storage dir + environment-derived defaults via
// configureSettings(); everything else is plain fs + JSON with validation and
// safe fallbacks when the file is missing or corrupt.
import { mkdirSync, readFileSync, writeFileSync } from 'fs'
import { join } from 'path'
import type { AppSettings, Preset } from '../shared/ipc.ts'

const PRESETS: Preset[] = ['fastest', 'fast', 'default', 'better']

type Listener = (settings: AppSettings) => void

interface SettingsEnv {
  /** Directory to store settings.json in (userData in production). */
  dir: string
  defaultThreads: number
  defaultOutputDir: string
}

const env: SettingsEnv = { dir: '', defaultThreads: 1, defaultOutputDir: '' }
const listeners = new Set<Listener>()
let cache: AppSettings | null = null

/** Inject the storage dir + defaults the module can't derive on its own. */
export function configureSettings(opts: Partial<SettingsEnv>): void {
  if (opts.dir !== undefined) env.dir = opts.dir
  if (opts.defaultThreads !== undefined) env.defaultThreads = Math.max(1, opts.defaultThreads)
  if (opts.defaultOutputDir !== undefined) env.defaultOutputDir = opts.defaultOutputDir
  cache = null // re-derive defaults against the new env on next load
}

function filePath(): string {
  return join(env.dir, 'settings.json')
}

function defaults(): AppSettings {
  return {
    threads: env.defaultThreads,
    defaultOutputDir: env.defaultOutputDir,
    defaultPreset: 'default',
    theme: 'dark'
  }
}

/** Coerce arbitrary parsed JSON into a valid AppSettings, filling gaps. */
function sanitize(raw: unknown): AppSettings {
  const d = defaults()
  if (!raw || typeof raw !== 'object') return d
  const o = raw as Record<string, unknown>
  const threads =
    typeof o.threads === 'number' && Number.isFinite(o.threads)
      ? Math.min(1024, Math.max(1, Math.round(o.threads)))
      : d.threads
  const defaultOutputDir =
    typeof o.defaultOutputDir === 'string' && o.defaultOutputDir.length > 0
      ? o.defaultOutputDir
      : d.defaultOutputDir
  const defaultPreset = PRESETS.includes(o.defaultPreset as Preset)
    ? (o.defaultPreset as Preset)
    : d.defaultPreset
  const theme = o.theme === 'light' || o.theme === 'dark' ? o.theme : d.theme
  return { threads, defaultOutputDir, defaultPreset, theme }
}

/** Load settings from disk (creating the file with defaults if missing). */
export function loadSettings(): AppSettings {
  if (cache) return cache
  let parsed: unknown = null
  try {
    parsed = JSON.parse(readFileSync(filePath(), 'utf-8'))
  } catch {
    // Missing or corrupt → fall back to defaults and rewrite a clean file.
    parsed = null
  }
  const settings = sanitize(parsed)
  cache = settings
  save(settings)
  return settings
}

/** Current settings (loads on first access). */
export function getSettings(): AppSettings {
  return cache ?? loadSettings()
}

function save(settings: AppSettings): void {
  try {
    mkdirSync(env.dir, { recursive: true })
    writeFileSync(filePath(), JSON.stringify(settings, null, 2), 'utf-8')
  } catch {
    // Non-fatal: keep the in-memory value even if the disk write fails.
  }
}

/** Merge a partial update, validate, persist, notify, and return the result. */
export function setSettings(partial: Partial<AppSettings>): AppSettings {
  const merged = sanitize({ ...getSettings(), ...partial })
  cache = merged
  save(merged)
  for (const l of listeners) l(merged)
  return merged
}

/** Subscribe to settings changes (main-side). Returns an unsubscribe fn. */
export function onSettingsChange(cb: Listener): () => void {
  listeners.add(cb)
  return () => listeners.delete(cb)
}
