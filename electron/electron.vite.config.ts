import { resolve } from "path"
import { defineConfig, externalizeDepsPlugin } from "electron-vite"
import react from "@vitejs/plugin-react"
import tailwindcss from "@tailwindcss/vite"

export default defineConfig({
  main: {
    // mscompress is a devDependency (so electron-builder doesn't copy the
    // symlinked node-ts tree into the asar) but must stay external: it is
    // shipped via extraResources and loads its .node addon relative to itself.
    plugins: [externalizeDepsPlugin({ include: ["mscompress"] })],
    build: {
      rollupOptions: {
        input: {
          index: resolve("src/main/index.ts"),
          "convert-worker": resolve("src/main/bindings/convert-worker.ts"),
        },
        // Keep shared chunks flat in out/main (not out/main/chunks) so the
        // `./convert-worker.js` URL resolved from the shared bindings chunk lands
        // on the emitted worker file next to it.
        output: {
          chunkFileNames: "[name]-[hash].js",
        },
      },
    },
  },
  preload: {
    plugins: [externalizeDepsPlugin()],
  },
  renderer: {
    resolve: {
      alias: {
        "@": resolve("src/renderer/src"),
        "@shared": resolve("src/shared"),
      },
    },
    plugins: [react(), tailwindcss()],
  },
})
