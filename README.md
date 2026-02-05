# CoreToolkit

Repositorio C++ base para utilidades, propiedades, matematicas, parseo LAMMPS y dependencias embebidas.

## Build

```
conan install . --output-folder=build --build=missing
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=build/conan_toolchain.cmake
cmake --build build
```
