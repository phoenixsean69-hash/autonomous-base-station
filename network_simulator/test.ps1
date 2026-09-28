$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here

if (-not (Test-Path ".venv\Scripts\python.exe")) {
  throw "Run .\run.ps1 once first so the virtual environment and dependencies exist."
}

& ".\.venv\Scripts\python.exe" ".\test_two_subscribers.py"
