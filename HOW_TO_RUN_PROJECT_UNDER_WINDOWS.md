# Run on Windows

Install CMake 3.20+, a C++20 compiler (Visual Studio 2022/MSVC or MinGW-w64), Node.js and Python 3. The installed Nuxt 4.3.1 package requires Node.js `^20.19.0 || >=22.12.0`; check the package requirement again when upgrading Nuxt. Run backend commands from the repository root so `power_model.ini` is found. The [documentation index](docs/README.md) explains the model, parameters, and browser protocol.

Before building, check that your tools are available:

```powershell
cmake --version
node --version
npm.cmd --version
python --version
```

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

## Troubleshooting

- If the binary is missing, rebuild and check `bin/tsvra.exe` in the repository root. Restart the web development server after changing the backend executable path. `TSVRA_BIN` can point to a custom executable and `TSVRA_ROOT` to its project working directory.
- If CMake cannot find a compiler, use the Visual Studio preset or check your MinGW installation paths. The compiler and make executable paths above are examples, not required locations. Use separate build directories for different compilers; a directory configured for one generator cannot be reused with another generator.
- If `npm.ps1` is blocked, use `npm.cmd` as shown above. No execution-policy change is required for these commands.
- If web dependencies are missing, run `npm.cmd ci` inside `web-nuxt`, then start the development server again. Run `npm.cmd test`, `npm.cmd run build`, and `npm.cmd run test:integration` there to check the frontend and server/native link.
- If a backend run cannot find `power_model.ini`, check the working directory or pass an explicit `--power-config` path. The retained regression package in `docs/power-model-final/` is a separate workflow and does not supply the runtime INI.
- If `git pull` is blocked by local changes, inspect `git status` and preserve your work before updating. The build and run steps do not require discarding local changes.
