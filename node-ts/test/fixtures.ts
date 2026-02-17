import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
export const TEST_DATA_DIR = path.resolve(__dirname, "../../test/data");
export const MZML_PATH = path.join(TEST_DATA_DIR, "test.mzML");
export const MSZ_PATH = path.join(TEST_DATA_DIR, "test.msz");
