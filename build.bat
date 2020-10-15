@echo off
idf.py build
if errorlevel 1 goto end
copy build\MainControllerNew.bin z:

:end