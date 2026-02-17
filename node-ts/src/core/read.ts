import fs from 'node:fs';
import path from 'node:path';
import type { BaseFile } from '../files/base-file.js';
// Import to trigger factory registration
import { MZMLFile } from '../files/mzml-file.js';
import { MSZFile } from '../files/msz-file.js';

const MAGIC_TAG = 0x035F51B5;

/**
 * Detect file type by reading file header.
 *
 * @param filePath - Path to the file to detect
 * @returns File type ('mzML' or 'msz') or null if unknown
 */
function detectFiletype(filePath: string): 'mzML' | 'msz' | null {
  const fd = fs.openSync(filePath, 'r');
  const buf = Buffer.alloc(512);
  fs.readSync(fd, buf, 0, 512, 0);
  fs.closeSync(fd);

  // Check for MSZ magic tag (little-endian)
  if (buf.length >= 4) {
    const magic = buf.readUInt32LE(0);
    if (magic === MAGIC_TAG) {
      return 'msz';
    }
  }

  // Check for mzML (contains "indexedmzML" in first 512 bytes)
  if (buf.toString('utf-8').includes('indexedmzML')) {
    return 'mzML';
  }

  return null;
}

/**
 * Read and parse mzML or MSZ files.
 * Auto-detects file type and returns the appropriate class.
 *
 * @param filePath - Path to the mzML or MSZ file
 * @returns MZMLFile or MSZFile instance
 * @throws TypeError if path is not a string
 * @throws Error if file doesn't exist, is a directory, or type cannot be determined
 *
 * @example
 * ```typescript
 * const file = read('sample.mzML');
 * console.log(`Loaded ${file.spectra.length} spectra`);
 * file.close();
 * ```
 */
export function read(filePath: string): BaseFile {
  if (typeof filePath !== 'string') {
    throw new TypeError('Path must be a string.');
  }

  const resolved = path.resolve(filePath);

  if (!fs.existsSync(resolved)) {
    throw new Error(`The specified file does not exist: ${resolved}`);
  }

  const stat = fs.statSync(resolved);
  if (stat.isDirectory()) {
    throw new Error(`The specified path is a directory, not a file: ${resolved}`);
  }

  const filetype = detectFiletype(resolved);

  if (filetype === 'mzML') {
    return new MZMLFile(resolved);
  } else if (filetype === 'msz') {
    return new MSZFile(resolved);
  } else {
    throw new Error(`Could not determine file type for: ${resolved}`);
  }
}
