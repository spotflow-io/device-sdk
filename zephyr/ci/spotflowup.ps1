#Requires -Version 5.1
<#
.SYNOPSIS
    Spotflow Device SDK workspace setup script for Zephyr and nRF Connect SDK (NCS).

.DESCRIPTION
    This script automates the creation of a west workspace for building
    Spotflow Device SDK samples on supported boards.

.PARAMETER zephyr
    Use upstream Zephyr RTOS configuration.

.PARAMETER ncs
    Use Nordic nRF Connect SDK configuration.

.PARAMETER board
    The board ID to configure the workspace for (e.g., frdm_rw612, nrf7002dk).

.EXAMPLE
    .\spotflowup.ps1 -zephyr -board frdm_rw612

.EXAMPLE
    .\spotflowup.ps1 -ncs -board nrf7002dk
#>

[CmdletBinding()]
param(
    [switch]$zephyr,
    [switch]$ncs,
    [Parameter(Mandatory = $true)]
    [string]$board
)

# Configuration
$QuickstartJsonUrl = "https://rhusakazurepocstorage.z6.web.core.windows.net/quickstart.json"
$ManifestBaseUrl = "https://github.com/spotflow-io/device-sdk"

# Colors and formatting
function Write-Step {
    param([string]$Message)
    Write-Host "`n▶ " -ForegroundColor Cyan -NoNewline
    Write-Host $Message -ForegroundColor White
}

function Write-Info {
    param([string]$Message)
    Write-Host "  ℹ " -ForegroundColor Blue -NoNewline
    Write-Host $Message -ForegroundColor Gray
}

function Write-Success {
    param([string]$Message)
    Write-Host "  ✓ " -ForegroundColor Green -NoNewline
    Write-Host $Message -ForegroundColor White
}

function Write-Warning {
    param([string]$Message)
    Write-Host "  ⚠ " -ForegroundColor Yellow -NoNewline
    Write-Host $Message -ForegroundColor Yellow
}

function Write-ErrorMessage {
    param([string]$Message)
    Write-Host "  ✗ " -ForegroundColor Red -NoNewline
    Write-Host $Message -ForegroundColor Red
}

function Write-Banner {
    Write-Host ""
    Write-Host "╔══════════════════════════════════════════════════════════════╗" -ForegroundColor Cyan
    Write-Host "║           Spotflow Device SDK - Workspace Setup              ║" -ForegroundColor Cyan
    Write-Host "╚══════════════════════════════════════════════════════════════╝" -ForegroundColor Cyan
    Write-Host ""
}

function Exit-WithError {
    param(
        [string]$Message,
        [string]$Details = ""
    )
    Write-Host ""
    Write-ErrorMessage $Message
    if ($Details) {
        Write-Host "  Details: $Details" -ForegroundColor DarkGray
    }
    Write-Host ""
    Write-Host "If you believe this is a bug, please report it at:" -ForegroundColor Gray
    Write-Host "  https://github.com/spotflow-io/device-sdk/issues" -ForegroundColor Cyan
    Write-Host ""
    exit 1
}

function Find-NrfUtil {
    <#
    .SYNOPSIS
        Finds nrfutil on PATH or nrfutil-sdk-manager in VS Code/Cursor extensions.
    .OUTPUTS
        Hashtable with 'Path' (full path to executable) and 'Type' ('nrfutil' or 'sdk-manager')
        Returns $null if not found.
    #>
    
    # First, try to find nrfutil on PATH
    try {
        $nrfutilPath = Get-Command nrfutil -ErrorAction SilentlyContinue
        if ($nrfutilPath) {
            return @{
                Path = $nrfutilPath.Source
                Type = "nrfutil"
            }
        }
    }
    catch {
        # Continue to check extensions
    }
    
    # Check VS Code and Cursor extension directories for nrfutil-sdk-manager
    $isWindowsOS = ($env:OS -eq 'Windows_NT') -or ($IsWindows -eq $true)
    
    if ($isWindowsOS) {
        $extensionDirs = @(
            "$env:USERPROFILE\.vscode\extensions",
            "$env:USERPROFILE\.cursor\extensions",
            "$env:USERPROFILE\.vscode-insiders\extensions"
        )
        $sdkManagerExe = "nrfutil-sdk-manager.exe"
    }
    else {
        $extensionDirs = @(
            "$HOME/.vscode/extensions",
            "$HOME/.cursor/extensions",
            "$HOME/.vscode-insiders/extensions"
        )
        $sdkManagerExe = "nrfutil-sdk-manager"
    }
    
    foreach ($extDir in $extensionDirs) {
        if (Test-Path $extDir) {
            # Look for nordic-semiconductor.nrf-connect* extensions
            $nordicExtensions = Get-ChildItem -Path $extDir -Directory -Filter "nordic-semiconductor.nrf-connect*" -ErrorAction SilentlyContinue
            foreach ($ext in $nordicExtensions) {
                # The SDK manager is typically in a bin or nrfutil subdirectory
                $possiblePaths = @(
                    (Join-Path $ext.FullName "bin\$sdkManagerExe"),
                    (Join-Path $ext.FullName "nrfutil\$sdkManagerExe"),
                    (Join-Path $ext.FullName $sdkManagerExe)
                )
                
                foreach ($path in $possiblePaths) {
                    if (Test-Path $path) {
                        return @{
                            Path = $path
                            Type = "sdk-manager"
                        }
                    }
                }
            }
        }
    }
    
    return $null
}

function Install-NrfUtilSdkManager {
    param(
        [string]$NrfUtilPath
    )
    <#
    .SYNOPSIS
        Installs the sdk-manager command for nrfutil if not already present.
    .RETURNS
        $true if sdk-manager is available (already installed or newly installed), $false otherwise.
    #>
    
    # Check if sdk-manager is already installed
    try {
        $null = & $NrfUtilPath sdk-manager --version 2>&1
        if ($LASTEXITCODE -eq 0) {
            return $true
        }
    }
    catch {
        # Not installed, continue to install
    }
    
    # Install sdk-manager command
    Write-Info "Installing nrfutil sdk-manager command..."
    try {
        & $NrfUtilPath install sdk-manager 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-Success "nrfutil sdk-manager installed"
            return $true
        }
        else {
            Write-ErrorMessage "Failed to install nrfutil sdk-manager"
            return $false
        }
    }
    catch {
        Write-ErrorMessage "Failed to install nrfutil sdk-manager: $($_.Exception.Message)"
        return $false
    }
}

function Get-NcsVersionFromManifest {
    param(
        [string]$ManifestPath
    )
    <#
    .SYNOPSIS
        Extracts the NCS version from the manifest file (revision of the 'nrf' project).
    .OUTPUTS
        The NCS version string (e.g., "v3.1.1") or $null if not found.
    #>
    
    if (-not (Test-Path $ManifestPath)) {
        return $null
    }
    
    $content = Get-Content $ManifestPath -Raw
    
    # Parse YAML to find the nrf project revision
    # Look for pattern: name: nrf followed by revision: <version>
    if ($content -match 'name:\s*nrf[\s\S]*?revision:\s*([^\s\r\n]+)') {
        return $matches[1]
    }
    
    return $null
}

function Confirm-Action {
    param(
        [string]$Message,
        [bool]$DefaultYes = $true
    )
    $suffix = if ($DefaultYes) { "[Y/n]" } else { "[y/N]" }
    Write-Host "  ? " -ForegroundColor Magenta -NoNewline
    Write-Host "$Message $suffix " -NoNewline
    $response = Read-Host
    if ([string]::IsNullOrWhiteSpace($response)) {
        return $DefaultYes
    }
    return $response -match "^[Yy]"
}

function Read-Input {
    param(
        [string]$Prompt,
        [string]$Default
    )
    Write-Host "  ? " -ForegroundColor Magenta -NoNewline
    Write-Host "$Prompt " -NoNewline
    if ($Default) {
        Write-Host "[$Default] " -ForegroundColor DarkGray -NoNewline
    }
    $response = Read-Host
    if ([string]::IsNullOrWhiteSpace($response)) {
        return $Default
    }
    return $response
}

# Validate arguments
Write-Banner

if ($zephyr -and $ncs) {
    Exit-WithError "Cannot specify both -zephyr and -ncs" "Please use only one of these options."
}

if (-not $zephyr -and -not $ncs) {
    Exit-WithError "Must specify either -zephyr or -ncs" "Use -zephyr for upstream Zephyr or -ncs for nRF Connect SDK."
}

$sdkType = if ($zephyr) { "zephyr" } else { "ncs" }
Write-Info "SDK type: $($sdkType.ToUpper())"
Write-Info "Board ID: $board"

# Step 1: Download and parse quickstart.json
Write-Step "Downloading board configuration..."

try {
    $quickstartJson = Invoke-RestMethod -Uri $QuickstartJsonUrl -UseBasicParsing -ErrorAction Stop
    Write-Success "Configuration downloaded successfully"
}
catch {
    Exit-WithError "Failed to download board configuration" $_.Exception.Message
}

# Step 2: Find the board in the configuration
Write-Step "Looking up board '$board'..."

$boardConfig = $null

if ($sdkType -eq "ncs") {
    # NCS boards are directly in ncs.boards array
    if ($quickstartJson.ncs -and $quickstartJson.ncs.boards) {
        $boardConfig = $quickstartJson.ncs.boards | Where-Object { $_.id -eq $board } | Select-Object -First 1
    }
}
else {
    # Zephyr boards are nested in zephyr.vendors[*].boards
    if ($quickstartJson.zephyr -and $quickstartJson.zephyr.vendors) {
        foreach ($vendor in $quickstartJson.zephyr.vendors) {
            $found = $vendor.boards | Where-Object { $_.id -eq $board } | Select-Object -First 1
            if ($found) {
                $boardConfig = $found
                $vendorName = $vendor.name
                break
            }
        }
    }
}

if (-not $boardConfig) {
    $availableBoards = @()
    if ($sdkType -eq "ncs" -and $quickstartJson.ncs -and $quickstartJson.ncs.boards) {
        $availableBoards = $quickstartJson.ncs.boards | ForEach-Object { $_.id }
    }
    elseif ($sdkType -eq "zephyr" -and $quickstartJson.zephyr -and $quickstartJson.zephyr.vendors) {
        foreach ($vendor in $quickstartJson.zephyr.vendors) {
            $availableBoards += $vendor.boards | ForEach-Object { $_.id }
        }
    }
    
    $boardList = ($availableBoards | Where-Object { $_ }) -join ", "
    Exit-WithError "Board '$board' not found in $($sdkType.ToUpper()) configuration" "Available boards: $boardList"
}

Write-Success "Found board: $($boardConfig.name)"
if ($vendorName) {
    Write-Info "Vendor: $vendorName"
}
Write-Info "Board target: $($boardConfig.board)"
Write-Info "Manifest: $($boardConfig.manifest)"
Write-Info "SDK version: $($boardConfig.sdk_version)"
if ($boardConfig.sdk_toolchain) {
    Write-Info "Toolchain: $($boardConfig.sdk_toolchain)"
}
if ($boardConfig.blob) {
    Write-Info "Blob: $($boardConfig.blob)"
}

# Show callout if present (strip HTML for console display)
if ($boardConfig.callout) {
    $calloutText = $boardConfig.callout -replace '<[^>]+>', ''
    Write-Host ""
    Write-Warning "Note: $calloutText"
}

# Step 3: Determine and confirm workspace folder
Write-Step "Configuring workspace location..."

# Detect if running on Windows (works for both Windows PowerShell 5.1 and PowerShell Core)
$isWindowsOS = ($env:OS -eq 'Windows_NT') -or ($IsWindows -eq $true)

# Set default folder based on platform
if ($isWindowsOS) {
    $defaultFolder = "C:\spotflow-ws"
}
else {
    # Linux/macOS
    $defaultFolder = Join-Path $HOME "spotflow-ws"
}

$sdkDisplayName = if ($zephyr) { "Zephyr" } else { "nRF Connect SDK (NCS)" }
Write-Info "The workspace will contain all $sdkDisplayName files and your project."
if ($isWindowsOS) {
    Write-Warning "Tip: Avoid very long paths on Windows to prevent build issues."
}
Write-Host ""

$workspaceFolder = Read-Input "Enter workspace folder path" $defaultFolder

if ([string]::IsNullOrWhiteSpace($workspaceFolder)) {
    Exit-WithError "Workspace folder path cannot be empty"
}

# Normalize path
$workspaceFolder = [System.IO.Path]::GetFullPath($workspaceFolder)

if (Test-Path $workspaceFolder) {
    $items = Get-ChildItem -Path $workspaceFolder -Force -ErrorAction SilentlyContinue
    if ($items.Count -gt 0) {
        Write-Warning "Folder '$workspaceFolder' already exists and is not empty."
        if (-not (Confirm-Action "Continue anyway? This may cause issues." $false)) {
            Write-Info "Setup cancelled by user."
            exit 0
        }
    }
}

Write-Success "Workspace folder: $workspaceFolder"

# Step 4: Check prerequisites
Write-Step "Checking prerequisites..."

# Check Python
$pythonCmd = $null
foreach ($cmd in @("python", "python3")) {
    try {
        $version = & $cmd --version 2>&1
        if ($LASTEXITCODE -eq 0) {
            $pythonCmd = $cmd
            Write-Success "Python found: $version"
            break
        }
    }
    catch {
        # Continue to next command
    }
}

if (-not $pythonCmd) {
    Exit-WithError "Python not found" "Please install Python 3.10+ from https://www.python.org/ or follow Zephyr's Getting Started Guide."
}

# Check Git
try {
    $gitVersion = & git --version 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Success "Git found: $gitVersion"
    }
    else {
        throw "Git not found"
    }
}
catch {
    Exit-WithError "Git not found" "Please install Git from https://git-scm.com/"
}

# Step 5: Check SDK installation
$installSdk = $false
$nrfUtilInfo = $null
$requiredSdkVersion = $boardConfig.sdk_version
$requiredToolchain = $boardConfig.sdk_toolchain

if ($sdkType -eq "ncs") {
    # NCS: Check for nrfutil or nrfutil-sdk-manager
    Write-Step "Checking nRF Connect SDK toolchain setup..."
    
    $nrfUtilInfo = Find-NrfUtil
    
    if ($nrfUtilInfo) {
        if ($nrfUtilInfo.Type -eq "nrfutil") {
            Write-Success "Found nrfutil at: $($nrfUtilInfo.Path)"
            # Ensure sdk-manager command is installed
            if (-not (Install-NrfUtilSdkManager -NrfUtilPath $nrfUtilInfo.Path)) {
                Write-Warning "Could not install nrfutil sdk-manager command."
                Write-Warning "Toolchain installation will be skipped. Install it manually later."
                $nrfUtilInfo = $null
            }
        }
        else {
            Write-Success "Found nrfutil-sdk-manager at: $($nrfUtilInfo.Path)"
        }
    }
    else {
        Write-Warning "nrfutil not found on PATH and nrfutil-sdk-manager not found in VS Code/Cursor extensions."
        Write-Host ""
        Write-Info "The nRF Connect SDK toolchain will need to be installed manually."
        Write-Info "You can install nrfutil from: https://www.nordicsemi.com/Products/Development-tools/nRF-Util"
        Write-Info "Or install the nRF Connect for VS Code extension pack."
        Write-Host ""
    }
    
    if ($nrfUtilInfo) {
        Write-Host ""
        Write-Info "The toolchain for the NCS version will be installed after downloading the workspace."
        Write-Info "This is a one-time setup that may take several minutes."
        Write-Host ""
        
        if (Confirm-Action "Install NCS toolchain after workspace setup?" $true) {
            $installSdk = $true
        }
        else {
            Write-Warning "Skipping toolchain installation. You can install it manually later."
        }
    }
}
else {
    # Zephyr: Check for Zephyr SDK
    Write-Step "Checking Zephyr SDK installation..."
    
    $sdkInstalled = $false
    $toolchainInstalled = $false
    
    # Check common SDK locations (platform-specific)
    if ($isWindowsOS) {
        $sdkLocations = @(
            "$env:USERPROFILE\zephyr-sdk-$requiredSdkVersion",
            "$env:USERPROFILE\.local\zephyr-sdk-$requiredSdkVersion",
            "C:\zephyr-sdk-$requiredSdkVersion",
            "$env:LOCALAPPDATA\zephyr-sdk-$requiredSdkVersion"
        )
    }
    else {
        # Linux/macOS locations
        $sdkLocations = @(
            "$HOME/zephyr-sdk-$requiredSdkVersion",
            "$HOME/.local/zephyr-sdk-$requiredSdkVersion",
            "/opt/zephyr-sdk-$requiredSdkVersion",
            "/usr/local/zephyr-sdk-$requiredSdkVersion"
        )
    }
    
    # Also check ZEPHYR_SDK_INSTALL_DIR environment variable
    if ($env:ZEPHYR_SDK_INSTALL_DIR) {
        $additionalSdkLocations = @(
            $env:ZEPHYR_SDK_INSTALL_DIR,
            "$env:ZEPHYR_SDK_INSTALL_DIR\zephyr-sdk-$requiredSdkVersion"
        )
        $sdkLocations = $additionalSdkLocations + $sdkLocations
    }
    
    foreach ($sdkPath in $sdkLocations) {
        if (Test-Path "$sdkPath\sdk_version") {
            Write-Success "Found Zephyr SDK at: $sdkPath"
            $sdkInstalled = $true
            
            # Check if toolchain is installed
            if ($requiredToolchain) {
                $toolchainPath = Join-Path $sdkPath $requiredToolchain
                if (Test-Path $toolchainPath) {
                    Write-Success "Toolchain '$requiredToolchain' is installed"
                    $toolchainInstalled = $true
                }
                else {
                    Write-Warning "Toolchain '$requiredToolchain' not found in SDK"
                }
            }
            else {
                $toolchainInstalled = $true # No specific toolchain required
            }
            break
        }
    }
    
    if (-not $sdkInstalled -or -not $toolchainInstalled) {
        Write-Warning "Zephyr SDK $requiredSdkVersion$(if ($requiredToolchain) { " with toolchain '$requiredToolchain'" }) is not installed."
        Write-Host ""
        Write-Info "The Zephyr SDK will be installed to your user profile directory."
        Write-Info "This is a one-time setup that modifies your system."
        Write-Host ""
        
        if (Confirm-Action "Install Zephyr SDK $requiredSdkVersion$(if ($requiredToolchain) { " with '$requiredToolchain' toolchain" })?" $true) {
            $installSdk = $true
        }
        else {
            Write-Warning "Skipping SDK installation. You can install it manually later with:"
            Write-Host "    west sdk install --version $requiredSdkVersion$(if ($requiredToolchain) { " --toolchains $requiredToolchain" })" -ForegroundColor DarkGray
        }
    }
    else {
        Write-Success "Zephyr SDK is properly installed"
    }
}

# Step 6: Create workspace folder and setup virtual environment
Write-Step "Creating workspace folder..."

try {
    if (-not (Test-Path $workspaceFolder)) {
        New-Item -ItemType Directory -Path $workspaceFolder -Force | Out-Null
        Write-Success "Created folder: $workspaceFolder"
    }
    else {
        Write-Info "Folder already exists: $workspaceFolder"
    }
}
catch {
    Exit-WithError "Failed to create workspace folder" $_.Exception.Message
}

# Change to workspace folder
try {
    Set-Location $workspaceFolder
    Write-Success "Changed to workspace folder"
}
catch {
    Exit-WithError "Failed to change to workspace folder" $_.Exception.Message
}

# Step 7: Create Python virtual environment
Write-Step "Creating Python virtual environment..."

$venvPath = Join-Path $workspaceFolder ".venv"

try {
    if (Test-Path $venvPath) {
        Write-Info "Virtual environment already exists, reusing it"
    }
    else {
        & $pythonCmd -m venv .venv
        if ($LASTEXITCODE -ne 0) {
            throw "venv creation failed with exit code $LASTEXITCODE"
        }
        Write-Success "Virtual environment created"
    }
}
catch {
    Exit-WithError "Failed to create Python virtual environment" $_.Exception.Message
}

# Activate virtual environment
Write-Step "Activating virtual environment..."

$activateScript = Join-Path $venvPath "Scripts\Activate.ps1"
if (-not (Test-Path $activateScript)) {
    $activateScript = Join-Path $venvPath "bin/Activate.ps1"
}

try {
    . $activateScript
    Write-Success "Virtual environment activated"
}
catch {
    Exit-WithError "Failed to activate virtual environment" $_.Exception.Message
}

# Step 8: Install West
Write-Step "Installing West..."

try {
    & pip install --upgrade pip wheel 2>&1 | Out-Null
    & pip install west
    if ($LASTEXITCODE -ne 0) {
        throw "pip install west failed with exit code $LASTEXITCODE"
    }
    Write-Success "West installed successfully"
}
catch {
    Exit-WithError "Failed to install West" $_.Exception.Message
}

# Step 9: Initialize West workspace
Write-Step "Initializing West workspace..."

$manifestFile = "zephyr/manifests/$($boardConfig.manifest)"

Write-Info "Manifest URL: $ManifestBaseUrl"
Write-Info "Manifest file: $manifestFile"

try {
    $westInitOutput = & west init --manifest-url $ManifestBaseUrl --manifest-file $manifestFile . 2>&1
    if ($LASTEXITCODE -ne 0) {
        # Check if it's just already initialized
        if ($westInitOutput -match "already initialized") {
            Write-Warning "Workspace already initialized, continuing..."
        }
        else {
            throw "west init failed: $westInitOutput"
        }
    }
    else {
        Write-Success "West workspace initialized"
    }
}
catch {
    Exit-WithError "Failed to initialize West workspace" $_.Exception.Message
}

# Step 10: Update West workspace (download all dependencies)
Write-Step "Downloading dependencies (this may take several minutes)..."

try {
    & west update --fetch-opt=--depth=1 --narrow
    if ($LASTEXITCODE -ne 0) {
        throw "west update failed with exit code $LASTEXITCODE"
    }
    Write-Success "Dependencies downloaded"
}
catch {
    Exit-WithError "Failed to download dependencies" $_.Exception.Message
}

# Step 11: Install Python packages
Write-Step "Installing Python packages..."

try {
    & west packages pip --install
    if ($LASTEXITCODE -ne 0) {
        throw "west packages pip failed with exit code $LASTEXITCODE"
    }
    Write-Success "Python packages installed"
}
catch {
    Exit-WithError "Failed to install Python packages" $_.Exception.Message
}

# Step 12: Fetch blobs if required
if ($boardConfig.blob) {
    Write-Step "Fetching binary blobs for $($boardConfig.blob)..."
    
    try {
        & west blobs fetch $boardConfig.blob --auto-accept
        if ($LASTEXITCODE -ne 0) {
            throw "west blobs fetch failed with exit code $LASTEXITCODE"
        }
        Write-Success "Binary blobs fetched"
    }
    catch {
        Exit-WithError "Failed to fetch binary blobs" $_.Exception.Message
    }
}

# Step 13: Install SDK/toolchain if requested
if ($installSdk) {
    if ($sdkType -eq "ncs" -and $nrfUtilInfo) {
        # NCS: Use nrfutil sdk-manager to install toolchain
        Write-Step "Installing NCS toolchain..."
        
        # Find the manifest file to extract NCS version
        $manifestPath = Join-Path $workspaceFolder "$($boardConfig.spotflow_path)/zephyr/manifests/$($boardConfig.manifest)"
        $ncsVersion = Get-NcsVersionFromManifest -ManifestPath $manifestPath
        
        if (-not $ncsVersion) {
            Write-ErrorMessage "Could not determine NCS version from manifest"
            Write-Warning "You can install the toolchain manually with: nrfutil sdk-manager install --ncs-version <version>"
        }
        else {
            Write-Info "NCS version from manifest: $ncsVersion"
            
            # Build the command based on the type of tool found
            if ($nrfUtilInfo.Type -eq "nrfutil") {
                $sdkManagerCmd = $nrfUtilInfo.Path
                $sdkManagerArgs = @("sdk-manager", "install", "--ncs-version", $ncsVersion)
                $displayCmd = "nrfutil sdk-manager install --ncs-version $ncsVersion"
            }
            else {
                # Direct nrfutil-sdk-manager executable
                $sdkManagerCmd = $nrfUtilInfo.Path
                $sdkManagerArgs = @("install", "--ncs-version", $ncsVersion)
                $displayCmd = "nrfutil-sdk-manager install --ncs-version $ncsVersion"
            }
            
            Write-Info "Running: $displayCmd"
            
            try {
                & $sdkManagerCmd @sdkManagerArgs
                if ($LASTEXITCODE -ne 0) {
                    throw "SDK manager install failed with exit code $LASTEXITCODE"
                }
                Write-Success "NCS toolchain installed for version $ncsVersion"
            }
            catch {
                Write-ErrorMessage "Failed to install NCS toolchain: $($_.Exception.Message)"
                Write-Warning "You can try installing it manually with: $displayCmd"
            }
        }
    }
    else {
        # Zephyr: Use west sdk install
        Write-Step "Installing Zephyr SDK $requiredSdkVersion..."
        
        $sdkArgs = @("sdk", "install", "--version", $requiredSdkVersion)
        if ($requiredToolchain) {
            $sdkArgs += @("--toolchains", $requiredToolchain)
        }
        
        Write-Info "Running: west $($sdkArgs -join ' ')"
        
        try {
            & west @sdkArgs
            if ($LASTEXITCODE -ne 0) {
                throw "west sdk install failed with exit code $LASTEXITCODE"
            }
            Write-Success "Zephyr SDK installed"
        }
        catch {
            Write-ErrorMessage "Failed to install Zephyr SDK: $($_.Exception.Message)"
            Write-Warning "You can try installing it manually later with: west sdk install --version $requiredSdkVersion$(if ($requiredToolchain) { " --toolchains $requiredToolchain" })"
        }
    }
}

# Summary
Write-Host ""
Write-Host "╔══════════════════════════════════════════════════════════════╗" -ForegroundColor Green
Write-Host "║                    Setup Complete! ✓                         ║" -ForegroundColor Green
Write-Host "╚══════════════════════════════════════════════════════════════╝" -ForegroundColor Green
Write-Host ""
Write-Host "Workspace location: " -NoNewline
Write-Host $workspaceFolder -ForegroundColor Cyan
Write-Host "Board: " -NoNewline
Write-Host "$($boardConfig.name) ($($boardConfig.board))" -ForegroundColor Cyan
Write-Host "Spotflow module path: " -NoNewline  
Write-Host $boardConfig.spotflow_path -ForegroundColor Cyan
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "  1. Open " -NoNewline
Write-Host "$($boardConfig.spotflow_path)/zephyr/samples/logs/prj.conf" -NoNewline -ForegroundColor DarkGray
Write-Host " and add the required configuration options."
Write-Host ""
Write-Host "  2. Build and flash the Spotflow sample:"
Write-Host "     cd $($boardConfig.spotflow_path)/zephyr/samples/logs" -ForegroundColor DarkGray
Write-Host "     west build --pristine --board $($boardConfig.board)" -NoNewline -ForegroundColor DarkGray
if ($boardConfig.build_extra_args) {
    Write-Host " $($boardConfig.build_extra_args)" -NoNewline -ForegroundColor DarkGray
}
Write-Host ""
Write-Host "     west flash" -ForegroundColor DarkGray
Write-Host ""
Write-Host "For more information, visit: " -NoNewline
$quickstartUrlSuffix = if ($zephyr) { "zephyr" } else { "nordic-nrf-connect" }
Write-Host "https://docs.spotflow.io/quickstart/$quickstartUrlSuffix" -ForegroundColor Cyan
Write-Host ""
