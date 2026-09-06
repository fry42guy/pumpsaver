# Build PumpSaver with a persistent build directory.
#
#   .\build.ps1              compile
#   .\build.ps1 -Upload      compile then flash COM5
#   .\build.ps1 -Port COM7 -Upload
#   .\build.ps1 -Clean       throw away the cache and rebuild from scratch
#
# WHY THE BUILD PATH MATTERS: arduino-cli does not keep its temporary build
# directory between runs, so every plain `arduino-cli compile` recompiles the
# whole ESP32 core (119 files) and ESPAsyncWebServer + AsyncTCP (111 files) --
# about 20 MB of object files and ~97 seconds. Those never change. Pointing at
# a stable directory drops a rebuild to ~11 s, because only the 15 sketch files
# are recompiled.
#
# The Arduino IDE keeps its own persistent build folder, so the same applies
# there: the FIRST compile of a sketch is slow, the rest are not. Changing any
# board menu option (PSRAM, flash size, USB CDC) changes the FQBN and forces a
# full rebuild.

param([switch]$Upload, [switch]$Clean, [string]$Port = "COM5")

$cli = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$cfg = "C:\Users\RyanBaird\.arduinoIDE\arduino-cli.yaml"
$fqbn = "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi"
$sketch = $PSScriptRoot
$bp = "$env:LOCALAPPDATA\Temp\pumpsaver-build"

if ($Clean -and (Test-Path $bp)) { Remove-Item -LiteralPath $bp -Recurse -Force; "cleaned $bp" }

$sw = [Diagnostics.Stopwatch]::StartNew()
& $cli --config-file $cfg compile --fqbn $fqbn $sketch --build-path $bp --warnings default
if (-not $?) { Write-Error "compile failed"; exit 1 }
"built in $([math]::Round($sw.Elapsed.TotalSeconds,1)) s"

if ($Upload) {
    "uploading to $Port ..."
    & $cli --config-file $cfg upload -p $Port --fqbn $fqbn --input-dir "$bp" $sketch
    if ($?) { "flashed" } else { Write-Error "upload failed - for the first flash hold BOOT, tap RESET, release BOOT" }
}
