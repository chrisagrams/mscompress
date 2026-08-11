// E2E harness: boots the renderer under Vite (dev server, no Electron) and
// launches Puppeteer against it. The renderer only ever talks to the main
// process through `window.api`, so tests inject a deterministic stub for it
// (see stub.js) — this exercises the real UI wiring without the native binding.
import { createServer } from "vite"
import react from "@vitejs/plugin-react"
import tailwindcss from "@tailwindcss/vite"
import puppeteer from "puppeteer"
import { existsSync, readdirSync } from "node:fs"
import { resolve, dirname, join } from "node:path"
import { fileURLToPath } from "node:url"
import { homedir } from "node:os"
import { installApiStub, fixtures } from "./stub.js"

const __dirname = dirname(fileURLToPath(import.meta.url))
const electronRoot = resolve(__dirname, "../..")
const rendererRoot = resolve(electronRoot, "src/renderer")

/** Start a Vite dev server serving just the renderer. Returns { url, close }. */
export async function startRenderer() {
  const server = await createServer({
    configFile: false,
    root: rendererRoot,
    resolve: {
      alias: {
        "@": resolve(electronRoot, "src/renderer/src"),
        "@shared": resolve(electronRoot, "src/shared"),
      },
    },
    plugins: [react(), tailwindcss()],
    logLevel: "error",
    server: { host: "127.0.0.1", strictPort: false },
  })
  await server.listen()
  const { port } = server.httpServer.address()
  return { url: `http://127.0.0.1:${port}/`, close: () => server.close() }
}

// Candidate browser executables, tried in order. Chrome-for-Testing has no
// official Linux-arm64 build, so on arm boxes puppeteer's bundled download is
// unusable; fall back to a Playwright-managed Chromium (real arm64 build) or a
// system Chrome. On x86_64 CI the bundled Chromium is used directly.
function browserCandidates() {
  const out = []
  if (process.env.PUPPETEER_EXECUTABLE_PATH) out.push(process.env.PUPPETEER_EXECUTABLE_PATH)
  try {
    const p = puppeteer.executablePath()
    if (p) out.push(p)
  } catch {
    /* no bundled browser configured */
  }
  const pwRoot = join(homedir(), ".cache", "ms-playwright")
  if (existsSync(pwRoot)) {
    for (const dir of readdirSync(pwRoot)) {
      if (dir.startsWith("chromium-") && !dir.includes("headless")) {
        out.push(join(pwRoot, dir, "chrome-linux", "chrome"))
      }
    }
  }
  out.push(
    "/usr/bin/google-chrome-stable",
    "/usr/bin/google-chrome",
    "/usr/bin/chromium",
    "/usr/bin/chromium-browser",
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Chromium.app/Contents/MacOS/Chromium",
  )
  return out
}

/** Launch a headless Chromium, trying candidates until one actually starts. */
export async function launchBrowser() {
  const args = ["--no-sandbox", "--disable-setuid-sandbox", "--disable-dev-shm-usage"]
  let lastErr
  for (const executablePath of browserCandidates()) {
    if (!existsSync(executablePath)) continue
    try {
      return await puppeteer.launch({ executablePath, headless: true, args })
    } catch (e) {
      lastErr = e
    }
  }
  // Last resort: let puppeteer resolve on its own.
  try {
    return await puppeteer.launch({ headless: true, args })
  } catch (e) {
    throw new Error(
      `Could not launch any Chromium. Tried: ${browserCandidates().join(", ")}\nLast error: ${
        lastErr?.message ?? e.message
      }`,
    )
  }
}

/**
 * Open a fresh page with the window.api stub installed, navigate to the app,
 * and wait for the shell to mount. Collects uncaught page errors on `page._errors`.
 */
export async function openApp(browser, url) {
  const page = await browser.newPage()
  await page.setViewport({ width: 1440, height: 900 })
  const errors = []
  page.on("pageerror", (e) => errors.push(e))
  page._errors = errors
  // Runs before any page script — bypasses the page CSP and defines window.api.
  await page.evaluateOnNewDocument(installApiStub, fixtures)
  await page.goto(url, { waitUntil: "domcontentloaded", timeout: 60_000 })
  await page.waitForSelector("header", { timeout: 60_000 })
  return page
}
