# Builds Amat for Windows and packs it as dist\Amat-windows.zip: amat.exe
# with the Qt DLLs and QML modules beside it, ready to unzip and run.
#
# Needs Qt 6 for MSVC (qmake and windeployqt on PATH) and a Visual Studio
# developer shell (cl and nmake on PATH). The GitHub workflow sets both up;
# locally, open "x64 Native Tools Command Prompt for VS", add Qt's bin
# folder to PATH, and run: powershell -File windows\build.ps1
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build-windows"
$dist = Join-Path $root "dist"
$app = Join-Path $dist "Amat"

New-Item -ItemType Directory -Force -Path $build | Out-Null
Push-Location $build
try {
    qmake (Join-Path $root "amat.pro") "CONFIG+=release"
    if ($LASTEXITCODE -ne 0) { throw "qmake failed" }
    nmake
    if ($LASTEXITCODE -ne 0) { throw "nmake failed" }
} finally {
    Pop-Location
}

if (Test-Path $dist) { Remove-Item -Recurse -Force $dist }
New-Item -ItemType Directory -Force -Path $app | Out-Null
Copy-Item (Join-Path $build "release\amat.exe") $app

# Qt's DLLs, plugins, and the QML modules the app imports, plus the MSVC
# runtime so it starts on a machine without Visual Studio.
windeployqt --release --compiler-runtime --no-translations `
    --qmldir (Join-Path $root "src") (Join-Path $app "amat.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed" }

# windeployqt adds Microsoft's runtime installer; ship the runtime DLLs
# themselves instead, so amat.exe starts without installing anything.
$redist = Get-ChildItem -Path (Join-Path $env:VCToolsRedistDir "x64") -Directory -Filter "Microsoft.VC*.CRT" | Select-Object -First 1
if (-not $redist) { throw "MSVC runtime DLLs not found under $env:VCToolsRedistDir" }
Copy-Item (Join-Path $redist.FullName "*.dll") $app
Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $app "vc_redist.x64.exe")

Copy-Item (Join-Path $root "LICENSE") (Join-Path $app "LICENSE.txt")
Copy-Item (Join-Path $root "windows\README.txt") (Join-Path $app "README.txt")

Compress-Archive -Path $app -DestinationPath (Join-Path $dist "Amat-windows.zip") -Force
Write-Host "Built $dist\Amat-windows.zip"
