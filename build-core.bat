@echo off
echo === Setting up VS 2019 environment ===
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64

echo === Configuring CMake ===
cmake -S . -B build-core -G "NMake Makefiles" > build-log.txt 2>&1
if %errorlevel% neq 0 (
    echo CMake configure failed!
    type build-log.txt
    exit /b 1
)

echo === Building ===
cmake --build build-core >> build-log.txt 2>&1
if %errorlevel% neq 0 (
    echo Build failed!
    type build-log.txt
    exit /b 1
)

echo === Running Tests ===
ctest --test-dir build-core --output-on-failure >> build-log.txt 2>&1
if %errorlevel% neq 0 (
    echo Tests failed!
    type build-log.txt
    exit /b 1
)

echo === Build Successful ===
type build-log.txt
