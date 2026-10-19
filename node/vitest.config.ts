import { defineConfig } from "vitest/config";
import path from "path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

export default defineConfig({
  test: {
    testTimeout: 30000,
    hookTimeout: 30000,
  },
  resolve: {
    alias: {
      "@": path.resolve(__dirname, "./src"),
      "@/core": path.resolve(__dirname, "./src/core"),
      "@/files": path.resolve(__dirname, "./src/files"),
      "@/mszx": path.resolve(__dirname, "./src/mszx"),
      "@/spectrum": path.resolve(__dirname, "./src/spectrum"),
      "@/types": path.resolve(__dirname, "./src/types"),
    },
  },
});
