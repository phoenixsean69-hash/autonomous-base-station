$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here

if (-not (Test-Path ".venv\Scripts\python.exe")) {
  throw "Run .\run.ps1 first so the simulator environment exists."
}

& ".\.venv\Scripts\python.exe" ".\test_base_station_telemetry.py"
