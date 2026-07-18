// End-to-end UI tests for the MScompress renderer, driven with Puppeteer against
// a Vite dev server. window.api is stubbed (see stub.js) so the tests exercise
// real component wiring — state, IPC calls, tab/kind logic — without Electron or
// the native binding. Run with: node --test test/e2e/app.test.js
import { test, before, after, describe } from "node:test"
import assert from "node:assert/strict"
import { startRenderer, launchBrowser, openApp } from "./harness.js"

let server
let browser

before(async () => {
  server = await startRenderer()
  browser = await launchBrowser()
})

after(async () => {
  await browser?.close()
  await server?.close()
})

// --- helpers ---------------------------------------------------------------

/** Text content of an element (first match), or '' if absent. */
async function textOf(page, selector) {
  const el = await page.$(selector)
  if (!el) return ""
  return page.evaluate((e) => e.textContent, el)
}

/** Whole-page text. Uses textContent (not innerText): content inside the
 * overflow-clipped ScrollArea is present but not "rendered", so innerText omits it. */
function bodyText(page) {
  return page.evaluate(() => document.body.textContent)
}

/** First element matching `selector` whose text includes `label`, or throws. */
async function findWithText(page, selector, label) {
  const handle = await page.evaluateHandle(
    (sel, txt) => {
      const els = Array.from(document.querySelectorAll(sel))
      return els.find((e) => e.textContent.trim().includes(txt)) || null
    },
    selector,
    label,
  )
  const el = handle.asElement()
  assert.ok(el, `expected a ${selector} containing "${label}"`)
  return el
}

/** Click the first element matching `selector` whose text includes `label`. */
async function clickWithText(page, selector, label) {
  const el = await findWithText(page, selector, label)
  await el.click()
}

/** Open the file dialog (stubbed) and wait for the Explorer entries to appear. */
async function openFixtureFiles(page) {
  await page.click('[data-testid="open-files"]')
  await page.waitForSelector('[data-testid="file-entry"]')
}

/** Select an Explorer file whose path ends with `ext` (e.g. '.msz'). */
async function selectFileByExt(page, ext) {
  const handle = await page.evaluateHandle((suffix) => {
    const els = Array.from(document.querySelectorAll('[data-testid="file-entry"]'))
    return els.find((e) => (e.getAttribute("data-path") || "").endsWith(suffix)) || null
  }, ext)
  const el = handle.asElement()
  assert.ok(el, `expected a file entry ending in "${ext}"`)
  await el.click()
}

const isDisabled = (page, selector) =>
  page.$eval(selector, (el) => el.disabled === true || el.hasAttribute("disabled"))

// --- tests -----------------------------------------------------------------

describe("MScompress renderer (e2e)", () => {
  test("boots with an empty file list and no crash", async () => {
    const page = await openApp(browser, server.url)
    try {
      // Empty-state placeholder renders in the workspace + inspector.
      await page.waitForSelector('[data-testid="empty-state"]')
      const empties = await page.$$('[data-testid="empty-state"]')
      assert.ok(empties.length >= 1, "empty state should render")

      // No files preloaded.
      const entries = await page.$$('[data-testid="file-entry"]')
      assert.equal(entries.length, 0, "file list should start empty")

      const text = await bodyText(page)
      assert.match(text, /No file open/)

      // Toolbar branding is the inline MscLogo SVG (aria-label), not text.
      const brand = await page.$('svg[aria-label="MScompress"]')
      assert.ok(brand, "toolbar branding (MscLogo) rendered")

      assert.deepEqual(page._errors, [], "no uncaught page errors")
    } finally {
      await page.close()
    }
  })

  test("opening files adds them to the Explorer and shows analysis in the Inspector", async () => {
    const page = await openApp(browser, server.url)
    try {
      await openFixtureFiles(page)

      const entries = await page.$$('[data-testid="file-entry"]')
      assert.equal(entries.length, 3, "three fixtures added")

      // Empty state replaced by the workspace once a file is selected.
      const centerEmpty = await page.$('main [data-testid="empty-state"]')
      assert.equal(centerEmpty, null, "workspace no longer shows empty state")

      // Inspector reflects the stubbed analyze() for the auto-selected mzML.
      await page.waitForFunction(() =>
        document.querySelector('[data-testid="inspector"]')?.textContent.includes("Spectrum count"),
      )
      const inspector = await textOf(page, '[data-testid="inspector"]')
      assert.match(inspector, /test\.mzML/)
      assert.match(inspector, /Spectrum count/)
      assert.match(inspector, /50/) // spectrumCount
      assert.match(inspector, /float64/) // m/z format
      assert.match(inspector, /MS1/)
      assert.match(inspector, /MS2/)
    } finally {
      await page.close()
    }
  })

  test("Convert operations grey out correctly by file kind", async () => {
    const page = await openApp(browser, server.url)
    try {
      await openFixtureFiles(page)
      await page.waitForSelector('[data-testid="op-compress"]')

      // mzML → compress + extract enabled, decompress disabled.
      assert.equal(await isDisabled(page, '[data-testid="op-compress"]'), false)
      assert.equal(await isDisabled(page, '[data-testid="op-decompress"]'), true)
      assert.equal(await isDisabled(page, '[data-testid="op-extract"]'), false)

      // Switch to the .msz fixture → the enablement inverts.
      await selectFileByExt(page, ".msz")
      await page.waitForFunction(
        () => {
          const c = document.querySelector('[data-testid="op-compress"]')
          const d = document.querySelector('[data-testid="op-decompress"]')
          return c && d && c.hasAttribute("disabled") && !d.hasAttribute("disabled")
        },
        { timeout: 10_000 },
      )
      assert.equal(await isDisabled(page, '[data-testid="op-compress"]'), true)
      assert.equal(await isDisabled(page, '[data-testid="op-decompress"]'), false)
    } finally {
      await page.close()
    }
  })

  test("Convert tab: presets render, Advanced accordion toggles, Output has Local + Remote", async () => {
    const page = await openApp(browser, server.url)
    try {
      await openFixtureFiles(page) // mzML selected → compress op → presets visible

      // Four presets render with their labels.
      for (const id of ["fastest", "fast", "default", "better"]) {
        await page.waitForSelector(`[data-testid="preset-${id}"]`)
      }
      const presetText = await textOf(page, '[data-testid="preset-better"]')
      assert.match(presetText, /Better/)

      // Advanced settings are collapsed until the trigger is clicked. Radix keeps
      // the content mounted but hidden (aria-expanded=false, zero-height box).
      const advTrigger = await findWithText(page, "button", "Advanced settings")
      assert.equal(
        await advTrigger.evaluate((e) => e.getAttribute("aria-expanded")),
        "false",
        "advanced accordion collapsed initially",
      )
      await advTrigger.click()
      await page.waitForFunction(
        () => {
          const t = Array.from(document.querySelectorAll("button")).find((b) =>
            b.textContent.trim().includes("Advanced settings"),
          )
          return t && t.getAttribute("aria-expanded") === "true"
        },
        { timeout: 10_000 },
      )
      const advanced = await textOf(page, '[data-testid="advanced-content"]')
      assert.match(advanced, /zstd level/) // advanced controls revealed

      // Output routing exposes Local + Remote (MSTransfer) tabs.
      const tabs = await page.$$eval('[role="tab"]', (els) => els.map((e) => e.textContent.trim()))
      assert.ok(
        tabs.some((t) => t.includes("Local")),
        "Local output tab present",
      )
      assert.ok(
        tabs.some((t) => t.includes("Remote")),
        "Remote (MSTransfer) output tab present",
      )
      await clickWithText(page, '[role="tab"]', "Remote")
      await page.waitForFunction(() => document.body.textContent.includes("Remote path"))
      assert.match(await bodyText(page), /Remote path/)
    } finally {
      await page.close()
    }
  })

  test("QC tab renders charts from computeQC()", async () => {
    const page = await openApp(browser, server.url)
    try {
      await openFixtureFiles(page)
      await clickWithText(page, '[role="tab"]', "QC")
      await page.waitForSelector('[data-testid="qc-charts"]')

      // recharts mounts SVGs once the panels have data + layout.
      await page.waitForSelector('[data-testid="qc-charts"] svg', { timeout: 15_000 })
      const svgs = await page.$$('[data-testid="qc-charts"] svg')
      assert.ok(svgs.length >= 2, "multiple charts rendered")

      const qc = await textOf(page, '[data-testid="qc-charts"]')
      assert.match(qc, /TIC Chromatogram/)
      assert.match(qc, /Base-Peak Chromatogram/)
      assert.match(qc, /Density Map/)
      assert.match(qc, /MS-Level Distribution/)
      assert.match(qc, /Peaks per Spectrum/)

      // Heatmap grid cells (plain divs) from the stubbed cells array.
      const cells = await page.$$('[data-testid="qc-charts"] [title^="density"]')
      assert.ok(cells.length > 0, "heatmap cells rendered")
    } finally {
      await page.close()
    }
  })

  test("Queue tab renders queue state from getQueueState()", async () => {
    const page = await openApp(browser, server.url)
    try {
      await clickWithText(page, '[role="tab"]', "Queue")
      await page.waitForSelector('[data-testid="queue-tab"]')
      // Wait for the job rows (queue state resolves async on mount).
      await page.waitForFunction(() =>
        document.querySelector('[data-testid="queue-tab"]')?.textContent.includes("test.mzML"),
      )
      const queue = await textOf(page, '[data-testid="queue-tab"]')
      assert.match(queue, /MSTransfer/)
      assert.match(queue, /test\.mzML/)
      assert.match(queue, /sciex_ttof6600_100\.mzML/)
      assert.match(queue, /1\/2 done/) // one job done out of two
    } finally {
      await page.close()
    }
  })

  test("Archive tab renders an MSZX manifest from readMszx()", async () => {
    const page = await openApp(browser, server.url)
    try {
      await openFixtureFiles(page)
      await selectFileByExt(page, ".mszx")
      await clickWithText(page, '[role="tab"]', "Archive")
      await page.waitForSelector('[data-testid="archive-manifest"]')
      const archive = await textOf(page, '[data-testid="archive-manifest"]')
      assert.match(archive, /MSZX Archive/)
      assert.match(archive, /scan_number/) // join key
      assert.match(archive, /psms\.percolator\.tsv/) // annotation
      assert.match(archive, /peptides\.pep\.xml/)
      assert.match(archive, /Comet search results/)
    } finally {
      await page.close()
    }
  })

  test("Settings dialog opens from the toolbar gear and shows threads/preset/output", async () => {
    const page = await openApp(browser, server.url)
    try {
      // Wait until persisted settings have loaded (footer shows "8 threads"),
      // which is also when the SettingsDialog becomes mountable.
      await page.waitForFunction(() => document.body.textContent.includes("8 threads"))
      await page.click('button[title="Settings"]')
      await page.waitForSelector('[data-testid="settings-dialog"]')
      const dialog = await textOf(page, '[data-testid="settings-dialog"]')
      assert.match(dialog, /Settings/)
      assert.match(dialog, /Worker threads/)
      assert.match(dialog, /8 \/ 16/) // threads / maxThreads
      assert.match(dialog, /Default compression preset/)
      assert.match(dialog, /Default output directory/)

      // The output dir lives in a readOnly <input> — assert its value, not text.
      const inputValues = await page.$$eval('[data-testid="settings-dialog"] input', (els) =>
        els.map((e) => e.value),
      )
      assert.ok(
        inputValues.some((v) => v.includes("home/lab/Downloads")),
        `expected an input showing the output dir, got ${JSON.stringify(inputValues)}`,
      )
    } finally {
      await page.close()
    }
  })
})
