import { MSZXBatchFile, MSZXFile } from "mscompress"
import type { AnnotationEntry } from "mscompress"
import type { MszxAnnotation, MszxArchive, MszxBatchManifest, MszxManifest } from "../../shared/ipc"
import { errorMessage } from "./formats"

/** Map native annotation records to the plain, IPC-safe shape. */
function mapAnnotations(list: AnnotationEntry[] | undefined): MszxAnnotation[] {
  return (list ?? []).map((a) => ({
    filename: a.filename,
    format: a.format,
    compressed: a.compressed,
    num_records: a.num_records ?? null,
    description: a.description ?? null,
  }))
}

/**
 * Read an .mszx archive's manifest + annotations into a plain, serializable
 * object — the v1 single-file shape or the v2 batch shape, discriminated on
 * `container`. Always resolves — a bad/missing archive is reported via `error`
 * (on the "single" shape, since the container is unknown at that point).
 */
export function readMszx(path: string): MszxArchive {
  const base: MszxManifest = {
    path,
    container: "single",
    version: "",
    created_at: "",
    spectra_file: "",
    num_spectra: 0,
    join_key: "",
    source_file: null,
    description: null,
    annotations: [],
  }

  // Sniff the container with the collection reader: it opens any .mszx (a v1
  // archive adapts to a one-member collection) and only reads manifest.json.
  let batchManifest: MszxBatchManifest | null = null
  let bf: MSZXBatchFile | null = null
  try {
    bf = MSZXBatchFile.open(path)
    if (bf.manifest.container === "batch") {
      batchManifest = {
        path,
        container: "batch",
        version: bf.manifest.version,
        description: bf.manifest.description ?? null,
        entries: bf.manifest.spectra_files.map((e) => ({
          entry: e.entry,
          original: e.original || null,
          size: e.size ?? 0,
          num_spectra: e.num_spectra ?? null,
          join_key: e.join_key ?? "",
          annotations: mapAnnotations(e.annotations),
        })),
      }
    }
  } catch (err) {
    return { ...base, error: errorMessage(err) }
  } finally {
    bf?.close()
  }
  if (batchManifest) return batchManifest

  // v1 → the flat reader, whose manifest carries the full single-file fields
  // (created_at etc.) that the batch adaptation drops.
  let file: MSZXFile | null = null
  try {
    file = MSZXFile.open(path)
    const m = file.manifest
    return {
      path,
      container: "single",
      version: m.version,
      created_at: m.created_at,
      spectra_file: m.spectra_file,
      num_spectra: m.num_spectra,
      join_key: m.join_key,
      source_file: m.source_file ?? null,
      description: m.description ?? null,
      annotations: mapAnnotations(m.annotations),
    }
  } catch (err) {
    return { ...base, error: errorMessage(err) }
  } finally {
    file?.close()
  }
}
