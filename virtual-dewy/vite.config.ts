import { defineConfig } from "vitest/config";
import { firmwarePlugin } from "./server/firmware-plugin.ts";

export default defineConfig({
  plugins: [firmwarePlugin()],
  // The dev server only needs src/, public/ and index.html. Refuse build folders and anything called secrets.h, even if
  // someone later copies one under this folder (setting `deny` replaces Vite's defaults, so they are repeated here).
  server: {
    host: "127.0.0.1", port: 5173, strictPort: false,
    fs: { deny: [".env", ".env.*", "*.{crt,pem}", "**/.git/**", "**/secrets.h", "**/*.secret", "**/.build/**", "**/.deps/**", "**/.zig-venv/**"] },
  },
  preview: { host: "127.0.0.1" },
  build: { target: "es2022", sourcemap: true },
  test: { environment: "node", include: ["test/**/*.test.ts"], testTimeout: 30000 },
});
