$ErrorActionPreference = "Stop"

Set-Location $PSScriptRoot

if (-not (Test-Path ".venv\Scripts\python.exe")) {
    Write-Host "[FAIL] BTS environment not installed."
    Write-Host "Run:"
    Write-Host "  .\setup.ps1"
    exit 1
}

Write-Host ""
Write-Host "=============================================="
Write-Host " BASIC BTS SIMULATOR"
Write-Host "=============================================="
Write-Host "BTS ID    : BTS-001"
Write-Host "HTTP      : http://127.0.0.1:8100"
Write-Host "Health    : http://127.0.0.1:8100/health"
Write-Host "State     : http://127.0.0.1:8100/state"
Write-Host "Telemetry : http://127.0.0.1:8100/telemetry"
Write-Host "WebSocket : ws://<LAPTOP-IP>:8100/ws/<subscriber-number>"
Write-Host ""
Write-Host "Press Ctrl+C to stop."
Write-Host ""

.\.venv\Scripts\python.exe -m uvicorn app:app --host 0.0.0.0 --port 8100
