import type { DivisionNative } from "@/types/types.js";
import { DataPositions } from "@/types/data-positions.js";

/**
 * Division containing position mappings for spectra, XML, m/z, and intensity data.
 * Used for file chunking and random access to spectrum data.
 */
export class Division {
  readonly spectra: DataPositions;
  readonly xml: DataPositions;
  readonly mz: DataPositions;
  readonly inten: DataPositions;
  readonly size: number;
  readonly scans: Uint32Array;
  readonly msLevels: Uint16Array;
  readonly retTimes: Float32Array | null;

  /**
   * Create Division from native division data.
   *
   * @param native - Native division data from C library
   */
  constructor(native: DivisionNative) {
    this.spectra = new DataPositions(native.spectra);
    this.xml = new DataPositions(native.xml);
    this.mz = new DataPositions(native.mz);
    this.inten = new DataPositions(native.inten);
    this.size = native.size;
    this.scans = native.scans;
    this.msLevels = native.msLevels;
    this.retTimes = native.retTimes.length > 0 ? native.retTimes : null;
  }
}
