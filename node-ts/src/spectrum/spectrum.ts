import type { BaseFile } from "../files/base-file.js";

/**
 * Represents a single mass spectrum with lazy-loaded data.
 * Provides access to m/z values, intensities, and metadata.
 */
export class Spectrum {
  readonly index: number;
  readonly scan: number;
  readonly msLevel: number;
  private _retentionTime: number;
  private _file: BaseFile;
  private _mz: Float64Array | Float32Array | null = null;
  private _intensity: Float64Array | Float32Array | null = null;
  private _xml: string | null = null;

  /**
   * Create a Spectrum instance.
   *
   * @param index - Zero-based spectrum index in the file
   * @param scan - Scan number from the instrument
   * @param msLevel - MS level (1 for MS1, 2 for MS/MS, etc.)
   * @param retentionTime - Retention time in seconds (may be NaN if not available)
   * @param file - Parent file for data access
   */
  constructor(index: number, scan: number, msLevel: number, retentionTime: number, file: BaseFile) {
    this.index = index;
    this.scan = scan;
    this.msLevel = msLevel;
    this._retentionTime = retentionTime;
    this._file = file;
  }

  /**
   * Get retention time in seconds.
   * Falls back to parsing XML if not available in binary data.
   *
   * @returns Retention time in seconds, or null if not available
   */
  get retentionTime(): number | null {
    if (Number.isNaN(this._retentionTime) || this._retentionTime === 0) {
      // Try to extract from XML
      const xml = this.xml;
      // Try both attribute orderings
      const match =
        xml.match(/accession="MS:1000016"[^>]*value="([^"]+)"/) ||
        xml.match(/value="([^"]+)"[^>]*accession="MS:1000016"/);
      if (match) {
        const value = parseFloat(match[1]);
        // Check for unit accession (minutes vs seconds)
        const unitMatch =
          xml.match(/accession="MS:1000016"[^/]*unitAccession="([^"]+)"/) ||
          xml.match(/unitAccession="([^"]+)"[^/]*accession="MS:1000016"/);
        if (unitMatch && unitMatch[1] === "UO:0000031") {
          // minutes -> seconds
          this._retentionTime = value * 60.0;
        } else {
          this._retentionTime = value;
        }
        return this._retentionTime;
      }
      if (this._retentionTime === 0) return 0;
      return null;
    }
    return this._retentionTime;
  }

  /**
   * Number of m/z-intensity pairs in this spectrum.
   */
  get size(): number {
    return this.mz.length;
  }

  /**
   * m/z array (mass-to-charge ratios).
   * Lazy-loaded and cached on first access.
   */
  get mz(): Float64Array | Float32Array {
    if (this._mz === null) {
      this._mz = this._file.getMzBinary(this.index);
    }
    return this._mz;
  }

  /**
   * Intensity array.
   * Lazy-loaded and cached on first access.
   */
  get intensity(): Float64Array | Float32Array {
    if (this._intensity === null) {
      this._intensity = this._file.getIntenBinary(this.index);
    }
    return this._intensity;
  }

  /**
   * Interleaved m/z and intensity values as [mz1, int1, mz2, int2, ...].
   *
   * @returns Float64Array with interleaved peak data
   * @throws Error if m/z and intensity arrays have different lengths
   */
  get peaks(): Float64Array {
    const mz = this.mz;
    const intensity = this.intensity;
    if (mz.length !== intensity.length) {
      throw new Error(
        `Mismatch in array lengths: mz has ${mz.length} elements, intensity has ${intensity.length} elements for spectrum ${this.index}`
      );
    }
    const result = new Float64Array(mz.length * 2);
    for (let i = 0; i < mz.length; i++) {
      result[i * 2] = mz[i];
      result[i * 2 + 1] = intensity[i];
    }
    return result;
  }

  /**
   * XML representation of the spectrum.
   * Lazy-loaded and cached on first access.
   */
  get xml(): string {
    if (this._xml === null) {
      this._xml = this._file.getXml(this.index);
    }
    return this._xml;
  }

  /**
   * Get string representation of the spectrum.
   *
   * @returns String describing the spectrum
   */
  toString(): string {
    return `Spectrum(index=${this.index}, scan=${this.scan}, msLevel=${this.msLevel}, retentionTime=${this.retentionTime})`;
  }
}
