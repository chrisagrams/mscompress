// Small formatting helpers shared across renderer components.

/** Human-readable byte size, e.g. 2411724800 → "2.2 GB". */
export function fmtBytes(n: number): string {
  const u = ["B", "KB", "MB", "GB", "TB"]
  let i = 0
  let v = n
  while (v >= 1024 && i < u.length - 1) {
    v /= 1024
    i++
  }
  return `${v.toFixed(v < 10 && i > 0 ? 1 : 0)} ${u[i]}`
}
