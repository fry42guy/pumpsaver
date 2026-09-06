# Builds the desktop harness. No Arduino, no board -- it compiles the real
# pump_control.h and plant_sim.h and co-simulates them.
#
#   .\build.ps1
#   .\harness.exe 1.5 300 0.01 55      # demand, seconds, substep, setpoint
#
# Uses the cut-down MSVC that ships inside Visual Studio's ScopeCppSDK, because
# this machine has no full C++ workload installed. Any g++ would do as well:
#   g++ -O2 -o harness harness.cpp

$sdk = "C:\Program Files\Microsoft Visual Studio\18\Community\SDK\ScopeCppSDK\vc15"
$cl  = "$sdk\VC\bin\cl.exe"
if (-not (Test-Path $cl)) { Write-Error "cl.exe not found at $cl"; exit 1 }

$env:INCLUDE = "$sdk\VC\include;$sdk\SDK\include\ucrt;$sdk\SDK\include\um;$sdk\SDK\include\shared"
$env:LIB     = "$sdk\VC\lib;$sdk\SDK\lib"

Push-Location $PSScriptRoot
& $cl /nologo /EHsc /O2 harness.cpp /Fe:harness.exe
$ok = $?
Pop-Location
if ($ok) { "built harness.exe" } else { Write-Error "build failed" }
