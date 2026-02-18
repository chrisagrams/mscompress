/**
 * MSZX convenience functions
 */

import { MSZFile } from "@/files/msz-file.js";
import { MSZXBuilder } from "@/mszx/mszx-builder.js";

/**
 * Create an MSZX archive from an MSZ file.
 *
 * Convenience function for simple archive creation.
 *
 * @param msz - MSZFile object
 * @param outputPath - Output path for the .mszx file
 * @param options - Creation options
 * @returns Promise resolving to the path of the created archive
 *
 * @example
 * ```typescript
 * import { read, createMSZX } from 'mscompress';
 *
 * const msz = read('sample.msz') as MSZFile;
 * await createMSZX(msz, 'sample.mszx', {
 *   annotations: ['sample.pin', 'sample.pepXML'],
 *   description: 'Annotated proteomics dataset'
 * });
 * ```
 */
export async function createMSZX(
  msz: MSZFile,
  outputPath: string,
  options?: {
    annotations?: string[];
    description?: string;
    joinKey?: string;
    extra?: Record<string, unknown>;
  }
): Promise<string> {
  const builder = new MSZXBuilder(msz);

  if (options?.description) {
    builder.setDescription(options.description);
  }

  if (options?.joinKey) {
    builder.setJoinKey(options.joinKey);
  }

  if (options?.extra) {
    for (const [key, value] of Object.entries(options.extra)) {
      builder.setExtra(key, value);
    }
  }

  if (options?.annotations) {
    for (const annotation of options.annotations) {
      builder.addAnnotations(annotation);
    }
  }

  return builder.save(outputPath);
}
