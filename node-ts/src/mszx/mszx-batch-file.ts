/**
 * Collection reader for MSZX archives.
 *
 * A v2 ("batch") archive bundles N independent MSZ files, so — unlike the
 * single-file {@link MSZXFile}, which *extends* MSZFile — the container cannot
 * itself be an MSZFile. MSZXBatchFile is a collection of them.
 *
 * It also opens a v1 archive, as a collection of one. That makes it the single
 * reader that handles every `.mszx` uniformly; MSZXFile remains the flat,
 * is-a-MSZFile reader when you specifically want v1's spectrum API.
 *
 * Members are opened lazily via `MSZFile.fromMszx()`, which mmaps the embedded
 * MSZ at its byte offset inside the tar. Nothing is extracted to a temp dir,
 * and only `manifest.json` is ever read into memory — a batch archive can hold
 * gigabytes of payload that must never be buffered.
 */

import * as fs from "node:fs";
import * as path from "node:path";
import * as tar from "tar";
import { MSZFile } from "@/files/msz-file.js";
import {
  MSZXBatchManifest,
  MSZXManifest,
  isBatchManifest,
  type MSZXManifestData,
  type SpectraFileEntryData,
} from "@/mszx/mszx-manifest.js";

/**
 * Read `manifest.json` out of an .mszx without buffering the payloads.
 *
 * `onReadEntry` fires for every member, so entries other than the manifest are
 * drained and discarded rather than accumulated.
 */
export function readArchiveManifestJSON(filePath: string): string {
  let manifest: string | undefined;
  tar.list({
    file: filePath,
    sync: true,
    onReadEntry: (entry) => {
      if (entry.path !== "manifest.json") {
        entry.resume(); // drain; do not buffer payloads
        return;
      }
      const chunks: Buffer[] = [];
      entry.on("data", (chunk: Buffer) => chunks.push(chunk));
      entry.on("end", () => {
        manifest = Buffer.concat(chunks).toString("utf-8");
      });
    },
  });
  if (manifest === undefined) {
    throw new Error("Invalid MSZX archive: missing manifest.json");
  }
  return manifest;
}

/**
 * Read the manifest of any `.mszx` as a batch manifest.
 *
 * A v2 archive parses directly; a v1 archive is adapted into a one-member
 * collection, with `size` recovered from the tar header the manifest omits.
 */
function readArchiveManifest(filePath: string): MSZXBatchManifest {
  const raw: unknown = JSON.parse(readArchiveManifestJSON(filePath));
  if (typeof raw !== "object" || raw === null) {
    throw new TypeError("Invalid manifest JSON: expected object");
  }
  if (isBatchManifest(raw as Record<string, unknown>)) {
    return MSZXBatchManifest.fromJSON(raw as never);
  }

  const adapted = MSZXBatchManifest.fromSingleFile(
    MSZXManifest.fromJSON(raw as MSZXManifestData)
  );
  const entry = adapted.spectra_files[0];
  if (entry) {
    tar.list({
      file: filePath,
      sync: true,
      onReadEntry: (member) => {
        if (member.path === entry.entry) entry.size = member.size;
        member.resume();
      },
    });
  }
  return adapted;
}

/**
 * An MSZX archive viewed as a collection of MSZ files.
 *
 * Holds N members for a v2 ("batch") archive and exactly one for a v1 archive,
 * so the same code handles either.
 *
 * @example
 * ```typescript
 * import { MSZXBatchFile } from "mscompress";
 *
 * const archive = MSZXBatchFile.open("cohort.mszx");
 * try {
 *   console.log(archive.length, archive.names);
 *   console.log(archive.entries[0].num_spectra); // from the manifest, no open
 *   for (const member of archive) {
 *     console.log(member.spectra.length);
 *   }
 * } finally {
 *   archive.close();
 * }
 * ```
 */
export class MSZXBatchFile {
  readonly archivePath: string;
  readonly manifest: MSZXBatchManifest;

  private readonly openMembers = new Map<string, MSZFile>();
  private closed = false;

  private constructor(archivePath: string, manifest: MSZXBatchManifest) {
    this.archivePath = archivePath;
    this.manifest = manifest;
  }

  /**
   * Open any `.mszx` for reading as a collection.
   *
   * A v2 batch archive yields N members; a v1 archive yields exactly one.
   *
   * @throws Error if the file is missing or has no manifest.
   */
  static open(filePath: string): MSZXBatchFile {
    const resolved = path.resolve(filePath);
    if (!fs.existsSync(resolved)) {
      throw new Error(`MSZX file not found: ${resolved}`);
    }
    return new MSZXBatchFile(resolved, readArchiveManifest(resolved));
  }

  /** Not available on a collection — pick a member first. */
  get spectra(): never {
    throw new Error(
      `MSZXBatchFile holds ${this.length} MSZ file(s) and has no single ` +
        `.spectra; use archive.get(0).spectra (or iterate the archive)`
    );
  }

  /** Number of MSZ members in the archive. */
  get length(): number {
    return this.manifest.spectra_files.length;
  }

  /** Manifest records, including `num_spectra` where the writer set it. */
  get entries(): SpectraFileEntryData[] {
    return this.manifest.spectra_files;
  }

  /** Tar member names of the MSZ payloads, in archive order. */
  get names(): string[] {
    return this.manifest.spectra_files.map((e) => e.entry);
  }

  /** True once {@link close} has run. */
  get isClosed(): boolean {
    return this.closed;
  }

  private entryFor(key: number | string): SpectraFileEntryData {
    const files = this.manifest.spectra_files;
    if (typeof key === "number") {
      const entry = files[key];
      if (!entry) {
        throw new RangeError(
          `entry index ${key} out of range (${files.length} entries)`
        );
      }
      return entry;
    }
    const entry = files.find((e) => e.entry === key);
    if (!entry) {
      throw new Error(`No such entry in archive: ${key}`);
    }
    return entry;
  }

  /**
   * Open a member by index or tar member name.
   *
   * The returned MSZFile is owned by this archive — it is cached and closed by
   * {@link close}. Do not close it yourself.
   */
  get(key: number | string): MSZFile {
    if (this.closed) {
      throw new Error("Operation on a closed MSZXBatchFile");
    }
    const entry = this.entryFor(key);
    let member = this.openMembers.get(entry.entry);
    if (!member) {
      member = MSZFile.fromMszx(this.archivePath, entry.entry);
      this.openMembers.set(entry.entry, member);
    }
    return member;
  }

  /** True if `name` is one of the archive's MSZ members. */
  has(name: string): boolean {
    return this.manifest.spectra_files.some((e) => e.entry === name);
  }

  *[Symbol.iterator](): Iterator<MSZFile> {
    for (let i = 0; i < this.length; i++) {
      yield this.get(i);
    }
  }

  /** Summary of the archive without opening any member. */
  describe(): Record<string, unknown> {
    return {
      path: this.archivePath,
      container: this.manifest.container,
      version: this.manifest.version,
      nEntries: this.length,
      description: this.manifest.description,
      entries: this.manifest.spectra_files,
    };
  }

  /**
   * Expand every member to `<outputDir>/<entry stem>.mzML`.
   *
   * @returns The written paths, in archive order.
   */
  decompress(outputDir: string): string[] {
    if (this.closed) {
      throw new Error("Operation on a closed MSZXBatchFile");
    }
    fs.mkdirSync(outputDir, { recursive: true });

    const written: string[] = [];
    for (const entry of this.manifest.spectra_files) {
      const stem = entry.entry.endsWith(".msz")
        ? entry.entry.slice(0, -4)
        : entry.entry;
      const target = path.join(outputDir, `${stem}.mzML`);
      this.get(entry.entry).decompress(target);
      written.push(target);
    }
    return written;
  }

  /** Close every opened member and release the archive. Idempotent. */
  close(): void {
    for (const member of this.openMembers.values()) {
      try {
        member.close();
      } catch {
        // Best-effort: one bad member must not strand the rest.
      }
    }
    this.openMembers.clear();
    this.closed = true;
  }

  [Symbol.dispose](): void {
    this.close();
  }
}
