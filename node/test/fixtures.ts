import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
export const TEST_DATA_DIR = path.resolve(__dirname, "../../test/data");
export const MZML_PATH = path.join(TEST_DATA_DIR, "test.mzML");
export const MSZ_PATH = path.join(TEST_DATA_DIR, "test.msz");
/** A checked-in byte-shuffled .msz (format 0.2). */
export const SHUFFLED_MSZ_PATH = path.join(TEST_DATA_DIR, "test_shuffled.msz");
export const MSZX_PATH = path.join(TEST_DATA_DIR, "mszx", "test.mszx");
export const SCIEX_MZML_PATH = path.join(TEST_DATA_DIR, "sciex_ttof6600_100.mzML");
export const CORRUPT_BASE64_MZML_PATH = path.join(TEST_DATA_DIR, "corrupt_base64.mzML");
/**
 * An mzML file's content up to and including </mzML>. Decompression rebuilds
 * the indexedmzML index that follows it, so a round trip matches the source
 * byte-for-byte only up to there unless the source's own index was correct
 * (test.mzML's is not: it was cut from a larger file).
 */
export function mzmlBody(buf: Buffer): Buffer {
  const end = buf.lastIndexOf("</mzML>");
  return end === -1 ? buf : buf.subarray(0, end + "</mzML>".length);
}
export const NONINDEXED_MZML_PATH = path.join(TEST_DATA_DIR, "nonindexed_test.mzML");
/** mzML files whose binaryDataArrays lack MS:1000574 / MS:1000576. */
export const NO_COMPRESSION_TERM_DIR = path.join(TEST_DATA_DIR, "no_compression_term");
