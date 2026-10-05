# Run on Windows

Install CMake 3.21+ for the Windows presets, a C++20 compiler (Visual Studio 2022/MSVC or MinGW-w64), Node.js and Python 3. Preset schema version 3 requires CMake 3.21 even though the native project itself accepts 3.20. The installed Nuxt 4.3.1 package requires Node.js `^20.19.0 || >=22.12.0`; check the package requirement again when upgrading Nuxt. Run backend commands from the repository root so `code/config/power_model.ini` is found. The [documentation index](README.md) explains the model, parameters, and browser protocol.

Before building, check that your tools are available:

```powershell
cmake --version
node --version
npm.cmd --version
python --version
```

## Visual Studio

```powershell
cd code/backend
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
cd ../..
.\code\build\windows-msvc\bin\tsvra.exe --cycles 1000 --seed 42 --output code/output/demo
```

## MinGW

Use paths matching your installation:

```powershell
cmake -S code/backend -B code/build/mingw -G "MinGW Makefiles" `
  -DCMAKE_CXX_COMPILER=C:/mingw64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=C:/mingw64/bin/mingw32-make.exe
cmake --build code/build/mingw --parallel
ctest --test-dir code/build/mingw --output-on-failure
.\code\build\mingw\bin\tsvra.exe --config code/config/config.ini
```

Normal runs export summary, requests and power CSVs plus exact configuration JSON. Example links retain vertical 1/horizontal 1000; every hop adds router delay (15 by default).

## Webpage and batches

```powershell
cd code/frontend
npm.cmd ci
npm.cmd run dev
```

Open Nuxt's printed address. `npm.cmd` avoids PowerShell's policy for `.ps1` launchers. The webpage starts the compiled backend automatically. From the repository root:

```powershell
python code/scripts/run_experiments.py --config code/config/experiments.example.json --jobs 4
```

## Troubleshooting

- If the binary is missing, rebuild and check the chosen build directory's `bin/tsvra.exe`. The web server and batch runner discover `code/build/bin/`, `code/build/windows-msvc/bin/`, and `code/build/mingw/bin/`; `TSVRA_BIN` (web) or `--binary` (batch) selects a custom executable. Restart the web development server after changing its executable path. `TSVRA_ROOT` selects the repository/runtime working directory.
- If CMake cannot find a compiler, use the Visual Studio preset or check your MinGW installation paths. The compiler and make executable paths above are examples, not required locations. Use separate build directories for different compilers; a directory configured for one generator cannot be reused with another generator.
- If `npm.ps1` is blocked, use `npm.cmd` as shown above. No execution-policy change is required for these commands.
- If web dependencies are missing, run `npm.cmd ci` inside `code/frontend`, then start the development server again. Run `npm.cmd test`, `npm.cmd run build`, and `npm.cmd run test:integration` there to check the frontend and server/native link.
- If a backend run cannot find `code/config/power_model.ini`, check the working directory or pass an explicit `--power-config` path. The retained regression package in `docs/power-model-final/` is a separate workflow and does not supply the runtime INI.
- If `git pull` is blocked by local changes, inspect `git status` and preserve your work before updating. The build and run steps do not require discarding local changes.
