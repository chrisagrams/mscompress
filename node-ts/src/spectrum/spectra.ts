import { Spectrum } from "./spectrum.js";
import { Division } from "../types/division.js";
import { DataFormat } from "../types/data-format.js";
import type { BaseFile } from "../files/base-file.js";

/**
 * Collection of spectra with lazy loading and caching.
 * Implements iterable interface for easy traversal.
 */
export class Spectra implements Iterable<Spectrum> {
  private _file: BaseFile;
  private _df: DataFormat;
  private _positions: Division;
  private _cache: (Spectrum | null)[];
  readonly length: number;

  /**
   * Create a Spectra collection.
   *
   * @param file - Parent file instance for data access
   * @param df - Data format information
   * @param positions - Position mappings for spectra
   */
  constructor(file: BaseFile, df: DataFormat, positions: Division) {
    this._file = file;
    this._df = df;
    this._positions = positions;
    this.length = this._df.sourceTotalSpec;
    this._cache = new Array<Spectrum | null>(this.length).fill(null);
  }

  /**
   * Get spectrum at the specified index with caching.
   *
   * @param index - Zero-based spectrum index
   * @returns Spectrum instance
   * @throws RangeError if index is out of bounds
   */
  get(index: number): Spectrum {
    if (index < 0 || index >= this.length) {
      throw new RangeError("Spectra index out of range");
    }

    if (this._cache[index] === null) {
      const retTime = this._positions.retTimes !== null ? this._positions.retTimes[index] : NaN;

      this._cache[index] = new Spectrum(
        index,
        this._positions.scans[index],
        this._positions.msLevels[index],
        retTime,
        this._file
      );
    }

    return this._cache[index];
  }

  /**
   * Iterator implementation for for...of loops.
   *
   * @returns Iterator over all spectra in the collection
   */
  [Symbol.iterator](): Iterator<Spectrum> {
    let i = 0;
    return {
      next: (): IteratorResult<Spectrum> => {
        if (i >= this.length) {
          return { done: true, value: undefined };
        }
        return { done: false, value: this.get(i++) };
      },
    };
  }
}
