import type { DataPositionsNative } from "./types.js";

/**
 * Position information for data blocks in a mass spectrometry file.
 * Tracks start and end positions for efficient random access.
 */
export class DataPositions {
  readonly startPositions: Float64Array;
  readonly endPositions: Float64Array;
  readonly totalSpec: number;

  /**
   * Create DataPositions from native position data.
   *
   * @param native - Native data positions from C library
   */
  constructor(native: DataPositionsNative) {
    this.startPositions = native.startPositions;
    this.endPositions = native.endPositions;
    this.totalSpec = native.totalSpec;
  }
}
