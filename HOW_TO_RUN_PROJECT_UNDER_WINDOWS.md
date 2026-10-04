# Run on Windows

Install CMake 3.20+, a C++20 compiler (Visual Studio 2022/MSVC or MinGW-w64), Node.js and Python 3. Run backend commands from the repository root so `power_model.ini` is found.

## Visual Studio

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
.\bin\tsvra.exe --cycles 1000 --seed 42 --output output/demo
```

## MinGW

Use paths matching your installation:

```powershell
cmake -S . -B build/mingw -G "MinGW Makefiles" `
  -DCMAKE_CXX_COMPILER=C:/mingw64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=C:/mingw64/bin/mingw32-make.exe
cmake --build build/mingw --parallel
ctest --test-dir build/mingw --output-on-failure
.\bin\tsvra.exe --config config.ini
```

Normal runs export summary, requests and power CSVs plus exact configuration JSON. Example links retain vertical 1/horizontal 1000; every hop adds router delay (15 by default).

## Webpage and batches

```powershell
cd web-nuxt
npm.cmd ci
npm.cmd run dev
```

Open Nuxt's printed address. `npm.cmd` avoids PowerShell's policy for `.ps1` launchers. The webpage starts the compiled backend automatically. From the repository root:

```powershell
python experiments.py --config experiments/example.json --jobs 4
```

If the binary is missing, rebuild and check `bin/tsvra.exe`. If CMake cannot find a compiler, use the Visual Studio preset or correct MinGW paths. Use separate build directories for different compilers.
