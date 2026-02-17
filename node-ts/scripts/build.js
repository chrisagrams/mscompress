#!/usr/bin/env node
import { execSync } from "child_process";

// Use "Unix Makefiles" on non-Windows to avoid Ninja, which doesn't
// support the add_custom_target approach used by ZlibInclude.cmake.
const generator = process.platform === "win32" ? "" : '-G "Unix Makefiles"';
execSync(`cmake-js compile ${generator}`.trim(), { stdio: "inherit" });
