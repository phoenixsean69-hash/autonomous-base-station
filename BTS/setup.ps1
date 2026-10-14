$ErrorActionPreference = "Stop"

Set-Location $PSScriptRoot

Write-Host ""
Write-Host "=============================================="
Write-Host " BASIC BTS - ENVIRONMENT SETUP"
Write-Host "=============================================="

if (-not (Test-Path ".venv")) {
    py -m venv .venv
}

.\.venv\Scripts\python.exe -m pip install --upgrade pip
.\.venv\Scripts\python.exe -m pip install -r requirements.txt

Write-Host ""
Write-Host "[PASS] BTS Python environment is ready."
Write-Host ""
Write-Host "Next:"
Write-Host "  .\run.ps1"
