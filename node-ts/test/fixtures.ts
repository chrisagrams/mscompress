import path from "node:path";

export const TEST_DATA_DIR = path.resolve(__dirname, "../../test/data");
export const MZML_PATH = path.join(TEST_DATA_DIR, "test.mzML");
export const MSZ_PATH = path.join(TEST_DATA_DIR, "test.msz");
