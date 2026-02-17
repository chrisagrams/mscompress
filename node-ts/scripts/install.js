#!/usr/bin/env node

/**
 * Post-install script for mscompress
 *
 * This runs after `npm install` and handles:
 * 1. Checking if the platform-specific binary package was installed
 * 2. Falling back to source build if needed (dev mode)
 * 3. Providing helpful error messages for unsupported platforms
 */

const { execSync } = require('child_process');
const fs = require('fs');
const path = require('path');
const os = require('os');

const platform = os.platform();
const arch = os.arch();
const platformKey = `${platform}-${arch}`;

const supportedPlatforms = {
  'darwin-x64': '@mscompress/darwin-x64',
  'darwin-arm64': '@mscompress/darwin-arm64',
  'linux-x64': '@mscompress/linux-x64',
  'linux-arm64': '@mscompress/linux-arm64',
  'win32-x64': '@mscompress/win32-x64'
};

function checkPlatformPackageInstalled() {
  const packageName = supportedPlatforms[platformKey];

  if (!packageName) {
    return false;
  }

  try {
    require.resolve(packageName);
    console.log(`✓ Using pre-built binary for ${platformKey} (${packageName})`);
    return true;
  } catch {
    return false;
  }
}

function attemptSourceBuild() {
  console.log(`⚠  Pre-built binary not available for ${platformKey}`);
  console.log('Attempting to build from source...');

  // Check if we're in development mode (source files exist)
  const cmakePath = path.join(__dirname, '..', 'CMakeLists.txt');
  const srcPath = path.join(__dirname, '..', 'src', 'native');

  if (!fs.existsSync(cmakePath) || !fs.existsSync(srcPath)) {
    console.log('ℹ  Source files not available in published package.');
    console.log('   This is expected - platform binaries are installed separately.');
    return;
  }

  try {
    console.log('Building native addon with cmake-js...');
    execSync('cmake-js compile', {
      stdio: 'inherit',
      cwd: path.join(__dirname, '..')
    });
    console.log('✓ Successfully built from source');
  } catch (error) {
    console.error('✗ Failed to build from source');
    console.error('\nTo build from source, you need:');
    console.error('  - Node.js development headers');
    console.error('  - C++ compiler (GCC, Clang, or MSVC)');
    console.error('  - CMake');
    console.error('  - Python 3');
    console.error('\nAlternatively, use a supported platform:');
    console.error('  ' + Object.keys(supportedPlatforms).join(', '));
    process.exit(1);
  }
}

// Main logic
if (!checkPlatformPackageInstalled()) {
  if (!supportedPlatforms[platformKey]) {
    console.error(`✗ Unsupported platform: ${platformKey}`);
    console.error(`\nSupported platforms:`);
    Object.keys(supportedPlatforms).forEach(p => {
      console.error(`  - ${p}`);
    });
    console.error('\nIf you need support for this platform, please open an issue:');
    console.error('  https://github.com/chrisagrams/mscompress/issues');
    attemptSourceBuild();
  } else {
    // Supported platform but package failed to install (network issue?)
    console.error(`✗ Failed to install platform package: ${supportedPlatforms[platformKey]}`);
    console.error('This may be due to a network issue. Try running: npm install again');
    attemptSourceBuild();
  }
}
