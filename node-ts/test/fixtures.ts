import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
export const TEST_DATA_DIR = path.resolve(__dirname, "../../test/data");
export const MZML_PATH = path.join(TEST_DATA_DIR, "test.mzML");
export const MSZ_PATH = path.join(TEST_DATA_DIR, "test.msz");
export const MSZX_PATH = path.join(TEST_DATA_DIR, "mszx", "test.mszx");
export const SCIEX_MZML_PATH = path.join(TEST_DATA_DIR, "sciex_ttof6600_100.mzML");
export const CORRUPT_BASE64_MZML_PATH = path.join(TEST_DATA_DIR, "corrupt_base64.mzML");
