@echo off
echo === Setting up VS 2019 environment ===
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64

echo === Cleaning ===
if exist build-qt rmdir /s /q build-qt

echo === Configuring CMake ===
cmake -S . -B build-qt -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:\Qt\6.5.3\msvc2019_64" -DBUILD_TESTING=OFF
if %errorlevel% neq 0 (
    echo CMake configure failed!
    exit /b 1
)

echo === Building ===
cmake --build build-qt
if %errorlevel% neq 0 (
    echo Build failed!
    exit /b 1
)

echo === Build Successful ===
