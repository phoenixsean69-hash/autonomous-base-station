$ErrorActionPreference = "Stop"

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here

if (-not (Test-Path ".venv")) {
  py -m venv .venv
}

& ".\.venv\Scripts\python.exe" -m pip install --upgrade pip
& ".\.venv\Scripts\python.exe" -m pip install -r requirements.txt

$lanIp = $null

try {
  $lanIp = Get-NetIPAddress `
    -AddressFamily IPv4 `
    -ErrorAction Stop |
    Where-Object {
      $_.IPAddress -ne "127.0.0.1" -and
      $_.IPAddress -notlike "169.254.*" -and
      $_.PrefixOrigin -ne "WellKnown"
    } |
    Sort-Object InterfaceMetric |
    Select-Object -First 1 -ExpandProperty IPAddress
}
catch {
  $lanIp = $null
}

Write-Host ""
Write-Host "ABS Mobile Network Simulator"
Write-Host "HTTP:      http://0.0.0.0:8000"

if ($lanIp) {
  Write-Host "Mobile app: ws://${lanIp}:8000"
}
else {
  Write-Host "Mobile app: ws://<LAPTOP-IP>:8000"
}

Write-Host ""
Write-Host "Subscriber A: 0712000001"
Write-Host "Subscriber B: 0712000002"
Write-Host ""
Write-Host "Keep this terminal running while the phones are connected."
Write-Host ""

& ".\.venv\Scripts\python.exe" -m uvicorn app:app --host 0.0.0.0 --port 8000
