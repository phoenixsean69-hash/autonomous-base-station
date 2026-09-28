$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here

if (-not (Test-Path ".venv")) {
  py -m venv .venv
}

& ".\.venv\Scripts\python.exe" -m pip install --upgrade pip
& ".\.venv\Scripts\python.exe" -m pip install -r requirements.txt

Write-Host ""
Write-Host "ABS Mobile Network Simulator"
Write-Host "HTTP:      http://0.0.0.0:8000"
Write-Host "WebSocket: ws://<LAPTOP-IP>:8000/ws/<subscriber>"
Write-Host ""
Write-Host "Subscriber A: 0712000001"
Write-Host "Subscriber B: 0712000002"
Write-Host ""
Write-Host "NOTE: reload mode is disabled for stable WebSocket testing."
Write-Host ""

& ".\.venv\Scripts\python.exe" -m uvicorn app:app --host 0.0.0.0 --port 8000
