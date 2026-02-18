import type { BaseFile } from "@/files/base-file.js";

type FileFactory = (path: string) => BaseFile;

const factories: Record<string, FileFactory> = {};

/**
 * Register a factory function for creating file instances.
 * Used internally to avoid circular dependencies between file types.
 *
 * @param type - File type identifier ('mzml' or 'msz')
 * @param factory - Factory function that creates a file instance from a path
 */
export function registerFileFactory(type: string, factory: FileFactory): void {
  factories[type] = factory;
}

/**
 * Create a file instance using a registered factory.
 *
 * @param type - File type identifier ('mzml' or 'msz')
 * @param filePath - Path to the file
 * @returns File instance (MZMLFile or MSZFile)
 * @throws Error if no factory is registered for the type
 */
export function createFile(type: string, filePath: string): BaseFile {
  const factory = factories[type];
  if (!factory) {
    throw new Error(`No factory registered for file type: ${type}`);
  }
  return factory(filePath);
}
