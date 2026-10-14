$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

Write-Host ""
Write-Host "=============================================="
Write-Host " BTS-001 HARDWARE BUILD"
Write-Host "=============================================="

$pioExe = $null

$cmd = Get-Command pio -ErrorAction SilentlyContinue
if ($cmd) {
    $pioExe = $cmd.Source
}

if (-not $pioExe) {
    $cmd = Get-Command platformio -ErrorAction SilentlyContinue
    if ($cmd) {
        $pioExe = $cmd.Source
    }
}

$candidates = @(
    "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe",
    "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe",
    "C:\Users\nooklyweb\Documents\PlatformIO\Projects\AutonomousBaseStation\.venv\Scripts\pio.exe",
    "C:\Users\nooklyweb\Documents\PlatformIO\Projects\AutonomousBaseStation\.venv\Scripts\platformio.exe"
)

if (-not $pioExe) {
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) {
            $pioExe = $candidate
            break
        }
    }
}

if (-not $pioExe) {
    Write-Host "[FAIL] PlatformIO Core was not found."
    Write-Host ""
    Write-Host "Checked:"
    foreach ($candidate in $candidates) {
        Write-Host "  $candidate"
    }
    Write-Host ""
    Write-Host "Open VS Code with the PlatformIO extension once, then retry."
    exit 1
}

Write-Host "[PASS] PlatformIO Core:"
Write-Host "  $pioExe"
Write-Host ""

& $pioExe run -e esp32dev

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "[FAIL] BTS hardware build failed."
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "[PASS] BTS-001 hardware build complete."
Write-Host ""
Write-Host "Next:"
Write-Host "  Open C:\Users\nooklyweb\Desktop\BTS\hardware in VS Code"
Write-Host "  F1 -> Wokwi: Start Simulator"
Write-Host ""
Write-Host "Expected RFC2217 endpoint:"
Write-Host "  rfc2217://localhost:4010"
