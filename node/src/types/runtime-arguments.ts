import native from "@/core/bindings.js";
import type { RuntimeArgumentsNative } from "@/types/types.js";

const ZSTD_COMPRESSION = 4700001;

/**
 * Runtime configuration arguments for compression and decompression operations.
 * Controls threading, block size, compression levels, and data format settings.
 */
export class RuntimeArguments {
  threads: number;
  blocksize: number;
  mzScaleFactor: number;
  intScaleFactor: number;
  targetXmlFormat: number;
  targetMzFormat: number;
  targetIntenFormat: number;
  zstdCompressionLevel: number;
  /** Byte-shuffle binary arrays before compressing; on by default. See {@link CompressArgs.shuffle}. */
  shuffle: boolean;

  /**
   * Create RuntimeArguments with default values.
   * Defaults: system thread count, 100MB blocks, ZSTD compression level 3,
   * byte shuffle on.
   */
  constructor() {
    this.threads = native.getNumThreads();
    this.blocksize = 100_000_000;
    this.mzScaleFactor = 1000;
    this.intScaleFactor = 0;
    this.targetXmlFormat = ZSTD_COMPRESSION;
    this.targetMzFormat = ZSTD_COMPRESSION;
    this.targetIntenFormat = ZSTD_COMPRESSION;
    this.zstdCompressionLevel = 3;
    this.shuffle = true;
  }

  /**
   * Convert to native arguments object for C library.
   *
   * @returns Native runtime arguments object
   */
  toNative(): RuntimeArgumentsNative {
    return {
      threads: this.threads,
      blocksize: this.blocksize,
      mzScaleFactor: this.mzScaleFactor,
      intScaleFactor: this.intScaleFactor,
      targetXmlFormat: this.targetXmlFormat,
      targetMzFormat: this.targetMzFormat,
      targetIntenFormat: this.targetIntenFormat,
      zstdCompressionLevel: this.zstdCompressionLevel,
      shuffle: this.shuffle,
    };
  }
}
