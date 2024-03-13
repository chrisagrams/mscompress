# mscompress
![Logo](electron/assets/logos/msc_logo.svg)


## Compilation
### Command-line Tool
To compile the CLI version of mscompress, follow these steps:

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

