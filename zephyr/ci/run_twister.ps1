param(
    [string]$TestPath = "tests/ble_framing",
    [string]$Platform = "native_sim"
)

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$moduleDir = Split-Path -Parent $scriptDir
$venvActivate = Join-Path $moduleDir "..\..\..\..\.venv\Scripts\Activate.ps1"

if (-not (Test-Path -LiteralPath $venvActivate)) {
    throw "Virtual environment activation script not found: $venvActivate"
}

. $venvActivate

$twister = Join-Path $moduleDir "..\..\..\..\zephyr\scripts\twister"
$resolvedTestPath = Join-Path $moduleDir $TestPath

if (-not (Test-Path -LiteralPath $twister)) {
    throw "Twister script not found: $twister"
}

if (-not (Test-Path -LiteralPath $resolvedTestPath)) {
    throw "Test suite path not found: $resolvedTestPath"
}

Push-Location $moduleDir
try {
    & $twister --platform $Platform --testsuite-root $resolvedTestPath
}
finally {
    Pop-Location
}
