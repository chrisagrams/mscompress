# MSZX archives

Bundle MSZ spectra with annotation files into a single tar archive.

## Build

```ts
import { MSZFile, MSZXBuilder } from "mscompress";

const msz = new MSZFile("spectra.msz");

await new MSZXBuilder()
  .setSpectra(msz)
  .setDescription("DDA run #42")
  .setJoinKey("scan_number")
  .addAnnotation({ name: "percolator", file: "results.pin", format: "percolator_pin" })
  .addAnnotation({ name: "comet", file: "search.pep.xml", format: "pepxml" })
  .setExtra({ instrument: "Orbitrap Eclipse" })
  .save("run42.mszx");

msz.close();
```

## Or use `createMSZX()`

```ts
import { createMSZX } from "mscompress";

await createMSZX({
  spectra: "spectra.msz",
  output: "run42.mszx",
  annotations: [
    { name: "percolator", file: "results.pin", format: "percolator_pin" },
  ],
  description: "DDA run #42",
  joinKey: "scan_number",
  extra: { instrument: "Orbitrap Eclipse" },
});
```

## Read

```ts
import { MSZXFile } from "mscompress";

const archive = new MSZXFile("run42.mszx");
console.log(archive.manifest.description);
console.log(archive.manifest.numSpectra);

for (const s of archive.spectra) {
  ...
}

archive.close();
```

See the [MSZX format spec](../../format/mszx.md) for the manifest schema.
