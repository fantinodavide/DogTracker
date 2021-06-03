@echo off
Setlocal enabledelayedexpansion

Set "Pattern=-min"
Set "Replace="

For %%# in ("G:\Il mio Drive\Arduino\DogTracker\data\vt\*.jpg") Do (
    Set "File=%%~nx#"
    Ren "%%#" "!File:%Pattern%=%Replace%!"
)

Pause&Exit