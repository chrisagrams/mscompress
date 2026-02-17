import type { DataFormatNative } from './types.js';

/** Mapping of numeric accession codes to PSI-MS controlled vocabulary strings */
const ACCESSION_MAP: Record<number, string> = {
  1000519: 'MS:1000519', // 32-bit integer
  1000520: 'MS:1000520', // 16-bit float
  1000521: 'MS:1000521', // 32-bit float
  1000522: 'MS:1000522', // 64-bit integer
  1000523: 'MS:1000523', // 64-bit double
  1000574: 'MS:1000574', // zlib compression
  1000576: 'MS:1000576', // no compression
  4700000: 'MS:4700000', // lossless
  4700001: 'MS:4700001', // ZSTD compression
};

/**
 * Convert numeric accession code to PSI-MS controlled vocabulary string.
 *
 * @param val - Numeric accession code
 * @returns PSI-MS accession string (e.g., 'MS:1000521')
 */
function accessionString(val: number): string {
  return ACCESSION_MAP[val] ?? `MS:${val}`;
}

/**
 * Data format information for mass spectrometry files.
 * Contains metadata about data encoding and compression formats.
 */
export class DataFormat {
  readonly sourceMzFmt: number;
  readonly sourceIntenFmt: number;
  readonly sourceCompression: number;
  readonly sourceTotalSpec: number;
  readonly targetXmlFormat: number;
  readonly targetMzFormat: number;
  readonly targetIntenFormat: number;
  readonly mzScaleFactor: number;
  readonly intScaleFactor: number;

  /**
   * Create a DataFormat instance from native format data.
   *
   * @param native - Native data format object from C library
   */
  constructor(native: DataFormatNative) {
    this.sourceMzFmt = native.sourceMzFmt;
    this.sourceIntenFmt = native.sourceIntenFmt;
    this.sourceCompression = native.sourceCompression;
    this.sourceTotalSpec = native.sourceTotalSpec;
    this.targetXmlFormat = native.targetXmlFormat;
    this.targetMzFormat = native.targetMzFormat;
    this.targetIntenFormat = native.targetIntenFormat;
    this.mzScaleFactor = native.mzScaleFactor;
    this.intScaleFactor = native.intScaleFactor;
  }

  /**
   * Get string representation of the data format.
   *
   * @returns String describing the format
   */
  toString(): string {
    return `DataFormat(sourceMzFmt=${this.sourceMzFmt}, sourceIntenFmt=${this.sourceIntenFmt}, sourceCompression=${this.sourceCompression}, sourceTotalSpec=${this.sourceTotalSpec})`;
  }

  /**
   * Convert to dictionary with PSI-MS accession strings.
   *
   * @returns Object mapping format properties to their PSI-MS accession strings
   */
  toDict(): Record<string, string | number> {
    return {
      sourceMzFmt: accessionString(this.sourceMzFmt),
      sourceIntenFmt: accessionString(this.sourceIntenFmt),
      sourceCompression: accessionString(this.sourceCompression),
      sourceTotalSpec: this.sourceTotalSpec,
    };
  }
}
