# HOW TO RUN TSVRA UNDER WINDOWS

This guide is for undergraduate students who only need to **use** the project on Windows.

The goal of this document is simple:

1. Update the project safely.
2. Build the C++ program correctly.
3. Run the command-line version.
4. Run the web version.

This guide assumes you are using **PowerShell** on Windows and the project folder is:

```powershell
D:\TSVRA
```

## Very Important Rules

Please follow these rules exactly:

1. You are **not allowed to modify source code**.
2. For Git, you should only use:

```powershell
git pull
```

3. Do **not** use `git add`, `git commit`, `git push`, `git checkout`, `git reset`, or other Git commands unless a maintainer explicitly tells you to do so.
4. If a command reports that local files were changed and `git pull` cannot continue, **stop** and contact the project maintainer.
5. For Windows on this project, use the **MinGW + CMake** method in this document. Do not switch to random Visual Studio tutorials.

## What You Need Installed

Before running the project, make sure these tools are available on the computer:

1. Git
2. CMake
3. MinGW-w64 compiler tools
4. Node.js and npm

This project has already been verified with the following Windows-style toolchain:

```powershell
CMake
D:\mingw64\bin\g++.exe
D:\mingw64\bin\mingw32-make.exe
node
npm.cmd
git
```

## Step 1: Open PowerShell and Go to the Project Folder

Open **PowerShell**, then run:

```powershell
cd D:\TSVRA
```

From this point on, unless the guide says otherwise, run commands inside `D:\TSVRA`.

## Step 2: Check Whether the Required Tools Are Available

Run these commands one by one:

```powershell
git --version
cmake --version
D:\mingw64\bin\g++.exe --version
D:\mingw64\bin\mingw32-make.exe --version
node --version
npm.cmd --version
```

If all of them print a version number, your environment is probably ready.

If one of them fails:

1. Do not guess.
2. Do not change project code.
3. Ask the maintainer or lab administrator to install the missing tool.

## Step 3: Update the Project Safely

Run:

```powershell
cd D:\TSVRA
git pull
```

What this does:

1. It downloads the newest project files from the remote repository.
2. It updates your local copy.

If `git pull` succeeds, continue to the next step.

If `git pull` says something like:

- `local changes would be overwritten`
- `please commit your changes`
- `please stash your changes`

then do **not** try to fix it yourself with extra Git commands. Stop and contact the maintainer.

## Step 4: Build the C++ Program with CMake

This project contains a C++ simulator. You must build it before running the command-line version or the web version.

### 4.1 Configure the project

Run:

```powershell
cd D:\TSVRA
cmake -S . -B build\mingw -G "MinGW Makefiles" `
  -DCMAKE_CXX_COMPILER=D:/mingw64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=D:/mingw64/bin/mingw32-make.exe
```

What this does:

1. `-S .` means the source code is in the current folder.
2. `-B build\mingw` means build files will be placed in `build\mingw`.
3. `-G "MinGW Makefiles"` tells CMake to use MinGW on Windows.
4. `-DCMAKE_CXX_COMPILER=...` tells CMake which C++ compiler to use.
5. `-DCMAKE_MAKE_PROGRAM=...` tells CMake which make tool to use.

Why this guide uses `build\mingw`:

1. It keeps Windows MinGW build files in one clean folder.
2. It avoids confusion with old or broken CMake cache files from other build methods.

### 4.2 Build the project

After configure succeeds, run:

```powershell
cmake --build build\mingw -j 8
```

What this does:

1. It compiles the C++ source files.
2. It links the final executable files.

If you want to run the make tool directly, the equivalent command is:

```powershell
D:\mingw64\bin\mingw32-make.exe -C build\mingw -j 8
```

For most students, `cmake --build ...` is easier and safer.

### 4.3 Check whether the build was successful

If the build succeeds, these files should exist:

```powershell
D:\TSVRA\bin\tsvra.exe
D:\TSVRA\bin\tsvra_tests.exe
```

You can confirm with:

```powershell
Get-ChildItem D:\TSVRA\bin
```

### 4.4 Run the tests

This step is recommended after a fresh `git pull` or after building on a new computer.

Run:

```powershell
ctest --test-dir build\mingw --output-on-failure
```

If the tests pass, the backend is ready.

## Step 5: Run the Command-Line Version

The simplest way to run the simulator is:

```powershell
cd D:\TSVRA
.\bin\tsvra.exe --config .\config.ini
```

This tells the program to load parameters from `config.ini`.

### Common command-line example

If you want to run without editing any configuration file, you can also use command-line parameters:

```powershell
.\bin\tsvra.exe `
  --layers 4 `
  --grid-factor 4 `
  --failure-mode c `
  --failure-rate 1e-9 `
  --vertical-delay 1 `
  --horizontal-delay 1000 `
  --cycles 100000 `
  --output demo_run
```

### Output files

After a normal command-line run, the program writes CSV files such as:

```powershell
demo_run_summary.csv
demo_run_requests.csv
```

If you use `config.ini` and keep the default output prefix, the files are usually:

```powershell
tsvra_output_summary.csv
tsvra_output_requests.csv
```

They will be created in the folder where you ran the command, which should normally be `D:\TSVRA`.

### Show help

If you want to see the supported options:

```powershell
.\bin\tsvra.exe --help
```

## Step 6: Run the Web Version

The project also contains a Nuxt web interface in:

```powershell
D:\TSVRA\web-nuxt
```

Important:

1. The web interface does **not** replace the C++ backend.
2. The web interface will try to start the compiled `tsvra.exe` automatically.
3. That means you must build the C++ program first, so that `D:\TSVRA\bin\tsvra.exe` exists.

### 6.1 Go to the web folder

Run:

```powershell
cd D:\TSVRA\web-nuxt
```

### 6.2 Install Node dependencies

Run:

```powershell
npm.cmd install
```

Why `npm.cmd` instead of `npm` in PowerShell:

On some Windows machines, PowerShell blocks `npm.ps1` because of script execution policy.
Using `npm.cmd` avoids that problem.

If you are using **Command Prompt (cmd.exe)** instead of PowerShell, `npm install` may also work, but in this guide we recommend `npm.cmd`.

### 6.3 Start the development server

Run:

```powershell
npm.cmd run dev
```

Then open your browser and visit:

```text
http://localhost:3000
```

Nuxt usually uses port `3000` by default.
If port `3000` is already occupied, the terminal may show a different local address. In that case, open the address printed in the terminal.

### 6.4 What to expect

If everything is correct:

1. The page opens in the browser.
2. The web app checks whether the backend binary exists.
3. When you start a simulation from the page, the web app launches the C++ program automatically.

You do **not** need to manually start `tsvra.exe` separately for the web UI.

## Step 7: Recommended Daily Workflow

If you just want to use the project normally, follow this routine:

### For command-line use

```powershell
cd D:\TSVRA
git pull
cmake -S . -B build\mingw -G "MinGW Makefiles" `
  -DCMAKE_CXX_COMPILER=D:/mingw64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=D:/mingw64/bin/mingw32-make.exe
cmake --build build\mingw -j 8
ctest --test-dir build\mingw --output-on-failure
.\bin\tsvra.exe --config .\config.ini
```

### For web use

```powershell
cd D:\TSVRA
git pull
cmake -S . -B build\mingw -G "MinGW Makefiles" `
  -DCMAKE_CXX_COMPILER=D:/mingw64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=D:/mingw64/bin/mingw32-make.exe
cmake --build build\mingw -j 8
cd .\web-nuxt
npm.cmd install
npm.cmd run dev
```

Then open `http://localhost:3000`.

## Step 8: Common Problems and What To Do

### Problem 1: `No CMAKE_CXX_COMPILER could be found`

Meaning:

1. CMake did not find a working Windows C++ compiler.

What to do:

1. Do not use `cmake ..` blindly.
2. Do not switch to random Visual Studio settings.
3. Use the exact MinGW configure command from this guide:

```powershell
cmake -S . -B build\mingw -G "MinGW Makefiles" `
  -DCMAKE_CXX_COMPILER=D:/mingw64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=D:/mingw64/bin/mingw32-make.exe
```

### Problem 2: PowerShell says `npm.ps1` cannot be loaded

Meaning:

1. PowerShell script execution policy blocked `npm`.

What to do:

Use:

```powershell
npm.cmd install
npm.cmd run dev
```

Do not waste time fighting PowerShell policy unless a maintainer tells you to.

### Problem 3: The web page says the binary is missing

Meaning:

1. The web app cannot find `tsvra.exe`.

What to do:

1. Go back to `D:\TSVRA`.
2. Build the C++ backend again.
3. Confirm this file exists:

```powershell
D:\TSVRA\bin\tsvra.exe
```

### Problem 4: `git pull` fails because of local changes

Meaning:

1. Something in your local folder is different from the remote repository.

What to do:

1. Stop immediately.
2. Do not run extra Git commands.
3. Contact the maintainer.

### Problem 5: `node_modules` is missing or the web app cannot start

Meaning:

1. The JavaScript dependencies are not installed yet.

What to do:

Run:

```powershell
cd D:\TSVRA\web-nuxt
npm.cmd install
```

### Problem 6: `D:\mingw64\bin\g++.exe` or `mingw32-make.exe` does not exist

Meaning:

1. The MinGW toolchain is not installed in the expected location.

What to do:

1. Ask the maintainer or lab administrator to install MinGW-w64 correctly.
2. Do not change project code to try to "work around" missing compiler tools.

## Step 9: Quick Copy-Paste Commands

If you only want the shortest usable command set, use the following.

### Backend only

```powershell
cd D:\TSVRA
git pull
cmake -S . -B build\mingw -G "MinGW Makefiles" `
  -DCMAKE_CXX_COMPILER=D:/mingw64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=D:/mingw64/bin/mingw32-make.exe
cmake --build build\mingw -j 8
ctest --test-dir build\mingw --output-on-failure
.\bin\tsvra.exe --config .\config.ini
```

### Web interface

```powershell
cd D:\TSVRA
git pull
cmake -S . -B build\mingw -G "MinGW Makefiles" `
  -DCMAKE_CXX_COMPILER=D:/mingw64/bin/g++.exe `
  -DCMAKE_MAKE_PROGRAM=D:/mingw64/bin/mingw32-make.exe
cmake --build build\mingw -j 8
cd .\web-nuxt
npm.cmd install
npm.cmd run dev
```

## Final Reminder

If you are a student using this repository, your job is to:

1. `git pull`
2. build
3. run

Your job is **not** to repair the codebase, rewrite build scripts, or use advanced Git operations.

When something unexpected happens, the safest action is to keep your files unchanged and ask the maintainer for help.
