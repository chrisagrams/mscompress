# mscompress
<img src="electron/assets/logos/msc_logo.svg" alt="Logo" width="400"/>
A Versatile Compression Tool for Efficient Storage of Mass-Spectrometry Data.


## Compilation
### Command-line Tool
To compile the CLI version of mscompress:

1. Clone the repository if you haven't already
```
git clone --recurse-submodules https://github.com/chrisagrams/mscompress_dev.git
```
2. Navigate to cli directory and create a build directory
```
cd mscompress_dev/cli
mkdir build
```
3. Navigate to build directory and configure the build
```
cd build
cmake ..
```
4. Build the executable
```
cmake --build ..
```
### NodeJS Native-API (NAPI) Library
To compile NAPI library:

1. Once cloned, navigate to `node/`

```
cd node/
```

2. Fetch dependencies and compile
```
npm install
```

### Electron GUI Application
To run/build Electron application:

1 . Navigate to `electron/`
```
cd electron/
```

2. To run in dev mode:
```
npm run start
```

3. To compile:
```
npm run build
```
