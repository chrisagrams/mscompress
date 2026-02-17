/** File type constants matching C core defines */
export const COMPRESS = 1; // mzML
export const DECOMPRESS = 2; // MSZ
export const EXTRACT = 3;
export const EXTRACT_MSZ = 4;
export const EXTERNAL = 5;

/** Options for spectrum extraction */
export interface ExtractOptions {
  indices?: number[];
  scanNumbers?: number[];
  msLevel?: number;
}

/** Native data format as returned from C */
export interface DataFormatNative {
  sourceMzFmt: number;
  sourceIntenFmt: number;
  sourceCompression: number;
  sourceTotalSpec: number;
  targetXmlFormat: number;
  targetMzFormat: number;
  targetIntenFormat: number;
  mzScaleFactor: number;
  intScaleFactor: number;
}

/** Native data positions */
export interface DataPositionsNative {
  startPositions: Float64Array;
  endPositions: Float64Array;
  totalSpec: number;
}

/** Native division */
export interface DivisionNative {
  spectra: DataPositionsNative;
  xml: DataPositionsNative;
  mz: DataPositionsNative;
  inten: DataPositionsNative;
  size: number;
  scans: Uint32Array;
  msLevels: Uint16Array;
  retTimes: Float32Array;
}

/** Native footer */
export interface FooterNative {
  xmlPos: number;
  mzBinaryPos: number;
  intenBinaryPos: number;
  xmlBlkPos: number;
  mzBinaryBlkPos: number;
  intenBinaryBlkPos: number;
  divisionsTPos: number;
  numSpectra: number;
  originalFilesize: number;
  nDivisions: number;
  mzFmt: number;
  intenFmt: number;
}

/** Arguments passed to native functions */
export interface RuntimeArgumentsNative {
  threads: number;
  blocksize: number;
  mzScaleFactor: number;
  intScaleFactor: number;
  targetXmlFormat: number;
  targetMzFormat: number;
  targetIntenFormat: number;
  zstdCompressionLevel: number;
  msLevel?: number;
  indices?: number[];
  scans?: number[];
}
