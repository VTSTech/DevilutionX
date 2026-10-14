@echo off
rmdir /q /s .\shaders\
echo Downloading libretro slang shaders ...
git clone https://github.com/libretro/slang-shaders shaders
cd shaders
echo Downloading PNG assets ...
git lfs pull
cd ..
echo Done
pause
