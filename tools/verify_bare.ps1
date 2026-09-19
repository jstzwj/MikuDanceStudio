# Bare (non-debugged) verification of the ray-mmd drop sequence.
# Usage: powershell -File verify_bare.ps1 [-Cycles 3]
param([int]$Cycles = 3)
$ErrorActionPreference = "Continue"
$rel = "C:\Users\jstzw\Documents\github\MikuDanceStudio\build-x64\build\Release"
$drop = "$rel\drop_test.ps1"
$results = @()
for ($i = 1; $i -le $Cycles; $i++) {
    Write-Output "=== RUN $i ==="
    $p = Start-Process -FilePath "$rel\MikuMikuDanceE.exe" -WorkingDirectory $rel -PassThru
    Start-Sleep -Seconds 12
    if ($p.HasExited) { Write-Output "RUN ${i}: exited during startup"; continue }
    & powershell -NoProfile -ExecutionPolicy Bypass -File $drop -ProcId $p.Id -Files "$rel\ray-mmd\ray.x" | Out-Null
    Start-Sleep -Seconds 95
    if ($p.HasExited) { Write-Output "RUN ${i}: CRASH after ray.x"; $results += "crash-ray.x"; continue }
    & powershell -NoProfile -ExecutionPolicy Bypass -File $drop -ProcId $p.Id -Files "$rel\ray-mmd\ray_controller.pmx" | Out-Null
    Start-Sleep -Seconds 25
    if ($p.HasExited) { Write-Output "RUN ${i}: CRASH after controller"; $results += "crash-controller"; continue }
    & powershell -NoProfile -ExecutionPolicy Bypass -File $drop -ProcId $p.Id -Files "$rel\ray-mmd\Skybox\Time of day\Time of day.pmx" | Out-Null
    Start-Sleep -Seconds 40
    if ($p.HasExited) { Write-Output "RUN ${i}: CRASH after tod"; $results += "crash-tod"; continue }
    Write-Output "RUN ${i}: SURVIVED"
    $results += "survived"
    Stop-Process -Id $p.Id -Force
    Start-Sleep -Seconds 3
}
Write-Output ("SUMMARY: " + ($results -join ", "))
