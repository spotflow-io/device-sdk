#Requires -Version 5.1
<#
.SYNOPSIS
    Spotflow Device SDK workspace setup script for Zephyr and nRF Connect SDK.

.DESCRIPTION
    This script automates the creation of a west workspace for building
    Spotflow Device SDK samples on supported boards.

.PARAMETER zephyr
    Use upstream Zephyr RTOS configuration.

.PARAMETER ncs
    Use Nordic nRF Connect SDK configuration.

.PARAMETER board
    The board ID to configure the workspace for (e.g., frdm_rw612, nrf7002dk).

.PARAMETER workspaceFolder
    The workspace folder path. If not specified, the script will prompt for it.

.PARAMETER spotflowRevision
    The Spotflow module revision to checkout. If not specified, the script will use the latest revision.

.PARAMETER autoConfirm
    Automatically confirm all actions without prompting (useful for CI/automated environments).

.EXAMPLE
    .\spotflowup.ps1 -zephyr -board frdm_rw612

.EXAMPLE
    .\spotflowup.ps1 -ncs -board nrf7002dk

.EXAMPLE
    .\spotflowup.ps1 -zephyr -board frdm_rw612 -workspaceFolder "C:\my-workspace" -autoConfirm
#>

[CmdletBinding()]
param(
    [switch]$zephyr,
    [switch]$ncs,
    [Parameter(Mandatory = $true)]
    [string]$board,
    [string]$workspaceFolder = "",
    [string]$spotflowRevision = "",
    [switch]$autoConfirm
)

# Configuration
$QuickstartJsonUrl = "https://rhusakazurepocstorage.z6.web.core.windows.net/quickstart.json"
$ManifestBaseUrl = "https://github.com/spotflow-io/device-sdk"

# Colors and formatting
function Write-Step {
    param([string]$Message)
    Write-Host "`n> " -ForegroundColor Cyan -NoNewline
    Write-Host $Message -ForegroundColor White
}

function Write-Info {
    param([string]$Message)
    Write-Host "  [i] " -ForegroundColor Blue -NoNewline
    Write-Host $Message -ForegroundColor Gray
}

function Write-Success {
    param([string]$Message)
    Write-Host "  [+] " -ForegroundColor Green -NoNewline
    Write-Host $Message -ForegroundColor White
}

function Write-Warning {
    param([string]$Message)
    Write-Host "  [!] " -ForegroundColor Yellow -NoNewline
    Write-Host $Message -ForegroundColor Yellow
}

function Write-ErrorMessage {
    param([string]$Message)
    Write-Host "  [x] " -ForegroundColor Red -NoNewline
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

function Confirm-Action {
    param(
        [string]$Message,
        [bool]$DefaultYes = $true
    )
    
    if ($autoConfirm) {
        Write-Info "$Message (auto-confirmed)"
        return $true
    }
    
    $suffix = if ($DefaultYes) { "[Y/n]" } else { "[y/N]" }
    Write-Host "  [?] " -ForegroundColor Magenta -NoNewline
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
        [string]$Default,
        [string]$ProvidedValue = ""
    )
    
    if (-not [string]::IsNullOrWhiteSpace($ProvidedValue)) {
        Write-Info "$Prompt (provided: $ProvidedValue)"
        return $ProvidedValue
    }
    
    if ($autoConfirm) {
        Write-Info "$Prompt (using default: $Default)"
        return $Default
    }

    Write-Host "  [?] " -ForegroundColor Magenta -NoNewline
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

function Test-NrfUtilHasSdkManager {
    param([string]$NrfUtilPath)
    <#
    .SYNOPSIS
        Checks if nrfutil has the sdk-manager command installed.
    .OUTPUTS
        $true if sdk-manager is available, $false otherwise.
    #>
    try {
        $null = & $NrfUtilPath sdk-manager --version 2>&1
        return ($LASTEXITCODE -eq 0)
    }
    catch {
        return $false
    }
}

function Find-NrfUtilSdkManagerInExtensions {
    <#
    .SYNOPSIS
        Finds nrfutil-sdk-manager in VS Code/Cursor extensions.
    .OUTPUTS
        Full path to the nrfutil-sdk-manager executable, or $null if not found.
    #>
    $isWindowsOS = ($env:OS -eq 'Windows_NT') -or ($IsWindows -eq $true)
    
    if ($isWindowsOS) {
        $extensionDirs = @(
            "$env:USERPROFILE\.vscode\extensions",
            "$env:USERPROFILE\.cursor\extensions",
            "$env:USERPROFILE\.vscode-insiders\extensions"
        )
        $sdkManagerRelPath = "platform\nrfutil\bin\nrfutil-sdk-manager.exe"
    }
    else {
        $extensionDirs = @(
            "$HOME/.vscode/extensions",
            "$HOME/.cursor/extensions",
            "$HOME/.vscode-insiders/extensions"
        )
        $sdkManagerRelPath = "platform/nrfutil/bin/nrfutil-sdk-manager"
    }
    
    foreach ($extDir in $extensionDirs) {
        if (-not (Test-Path $extDir)) {
            continue
        }
        
        $filterPattern = "nordic-semiconductor.nrf-connect*"
        $nordicExtensions = Get-ChildItem -Path $extDir -Directory `
            -Filter $filterPattern -ErrorAction SilentlyContinue
        
        foreach ($ext in $nordicExtensions) {
            $sdkManagerPath = Join-Path $ext.FullName $sdkManagerRelPath
            if (Test-Path $sdkManagerPath) {
                return $sdkManagerPath
            }
        }
    }
    
    return $null
}

function Find-NrfUtil {
    <#
    .SYNOPSIS
        Finds the best available nrfutil/sdk-manager for nRF Connect SDK
        toolchain management.
        Priority:
        1. nrfutil on PATH with sdk-manager already installed
        2. nrfutil-sdk-manager from VS Code/Cursor extension
        3. nrfutil on PATH without sdk-manager (needs installation)
    .OUTPUTS
        Hashtable with:
        - 'Path': full path to executable
        - 'Type': 'nrfutil' or 'sdk-manager'
        - 'NeedsSdkManagerInstall': $true if sdk-manager needs installation
        Returns $null if not found.
    #>

    $nrfutilOnPath = $null
    $nrfutilHasSdkManager = $false

    try {
        $nrfutilCmd = Get-Command nrfutil -ErrorAction SilentlyContinue
        if ($nrfutilCmd) {
            $nrfutilOnPath = $nrfutilCmd.Source
            $nrfutilHasSdkManager = Test-NrfUtilHasSdkManager $nrfutilOnPath
        }
    }
    catch {
        # nrfutil not on PATH
    }

    if ($nrfutilOnPath -and $nrfutilHasSdkManager) {
        return @{
            Path                   = $nrfutilOnPath
            Type                   = "nrfutil"
            NeedsSdkManagerInstall = $false
        }
    }

    $extensionSdkManager = Find-NrfUtilSdkManagerInExtensions
    if ($extensionSdkManager) {
        return @{
            Path                   = $extensionSdkManager
            Type                   = "sdk-manager"
            NeedsSdkManagerInstall = $false
        }
    }

    if ($nrfutilOnPath) {
        return @{
            Path                   = $nrfutilOnPath
            Type                   = "nrfutil"
            NeedsSdkManagerInstall = $true
        }
    }

    return $null
}

function Install-NrfUtilSdkManager {
    param([string]$NrfUtilPath)
    <#
    .SYNOPSIS
        Installs the sdk-manager command for nrfutil.
    .OUTPUTS
        $true if sdk-manager was installed successfully, $false otherwise.
    #>

    Write-Info "Installing nrfutil sdk-manager command..."
    try {
        & $NrfUtilPath install sdk-manager 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-Success "nrfutil sdk-manager installed"
            return $true
        }
        Write-ErrorMessage "Failed to install nrfutil sdk-manager"
        return $false
    }
    catch {
        $errorMsg = "Failed to install nrfutil sdk-manager: $($_.Exception.Message)"
        Write-ErrorMessage $errorMsg
        return $false
    }
}

function Test-NcsToolchainInstalled {
    param(
        [string]$NrfUtilPath,
        [string]$NrfUtilType,
        [string]$NcsVersion
    )
    <#
    .SYNOPSIS
        Checks if the nRF Connect SDK toolchain for the specified version
        is already installed.
    .OUTPUTS
        $true if the toolchain is installed, $false otherwise.
    #>

    try {
        $output = if ($NrfUtilType -eq "nrfutil") {
            & $NrfUtilPath sdk-manager toolchain list 2>&1
        }
        else {
            & $NrfUtilPath toolchain list 2>&1
        }
        
        if ($LASTEXITCODE -eq 0 -and $output) {
            return ($output -match [regex]::Escape($NcsVersion))
        }
    }
    catch {
        # If command fails, assume toolchain is not installed
    }

    return $false
}

# Validate arguments
Write-Banner

if ($zephyr -and $ncs) {
    $errorMsg = "Cannot specify both -zephyr and -ncs"
    $details = "Please use only one of these options."
    Exit-WithError $errorMsg $details
}

if (-not $zephyr -and -not $ncs) {
    $errorMsg = "Must specify either -zephyr or -ncs"
    $details = "Use -zephyr for upstream Zephyr or -ncs for nRF Connect SDK."
    Exit-WithError $errorMsg $details
}

$sdkType = if ($zephyr) { "zephyr" } else { "ncs" }
$sdkDisplayType = if ($zephyr) { "Zephyr" } else { "nRF Connect SDK" }
Write-Info "SDK type: $sdkDisplayType"
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
$vendorName = $null

if ($sdkType -eq "ncs" -and $quickstartJson.ncs -and $quickstartJson.ncs.boards) {
    $boardConfig = $quickstartJson.ncs.boards | 
        Where-Object { $_.id -eq $board } | 
        Select-Object -First 1
}
elseif ($sdkType -eq "zephyr" -and $quickstartJson.zephyr -and 
        $quickstartJson.zephyr.vendors) {
    foreach ($vendor in $quickstartJson.zephyr.vendors) {
        $found = $vendor.boards | 
            Where-Object { $_.id -eq $board } | 
            Select-Object -First 1
        if ($found) {
            $boardConfig = $found
            $vendorName = $vendor.name
            break
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
    $errorMsg = "Board '$board' not found in $sdkDisplayType configuration"
    Exit-WithError $errorMsg "Available boards: $boardList"
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
if ($boardConfig.ncs_version) {
    Write-Info "nRF Connect SDK version: $($boardConfig.ncs_version)"
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

Write-Info "The workspace will contain all $sdkDisplayType files and your project."
if ($isWindowsOS) {
    Write-Warning "Tip: Avoid very long paths on Windows to prevent build issues."
}
Write-Host ""

$workspaceFolder = Read-Input "Enter workspace folder path" $defaultFolder -ProvidedValue $workspaceFolder

if ([string]::IsNullOrWhiteSpace($workspaceFolder)) {
    Exit-WithError "Workspace folder path cannot be empty"
}

# Normalize path (resolve relative paths against PowerShell's CWD)
$workspaceFolder = [System.IO.Path]::GetFullPath(
    [System.IO.Path]::Combine($PWD.Path, $workspaceFolder)
)

if (Test-Path $workspaceFolder) {
    $items = Get-ChildItem -Path $workspaceFolder -Force `
        -ErrorAction SilentlyContinue
    if ($items.Count -gt 0) {
        Write-Warning "Folder '$workspaceFolder' already exists and is not empty."
        $confirmMsg = "Continue anyway? This may cause issues."
        if (-not (Confirm-Action $confirmMsg $false)) {
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
        continue
    }
}

if (-not $pythonCmd) {
    $details = "Please install Python 3.10+ from https://www.python.org/ " +
               "or follow Zephyr's Getting Started Guide."
    Exit-WithError "Python not found" $details
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
$installNcsToolchain = $false
$nrfUtilInfo = $null
$requiredSdkVersion = $boardConfig.sdk_version
$requiredToolchain = $boardConfig.sdk_toolchain
$ncsVersion = $boardConfig.ncs_version

if ($sdkType -eq "ncs") {
    Write-Step "Checking nRF Connect SDK toolchain setup..."

    $nrfUtilInfo = Find-NrfUtil

    if ($nrfUtilInfo) {
        if ($nrfUtilInfo.Type -eq "nrfutil") {
            if ($nrfUtilInfo.NeedsSdkManagerInstall) {
                Write-Info "Found nrfutil at: $($nrfUtilInfo.Path)"
                Write-Warning "The sdk-manager command is not installed for nrfutil."
                Write-Host ""
                $msg = "The sdk-manager command is required to install " +
                       "the nRF Connect SDK toolchain."
                Write-Info $msg
                Write-Host ""

                if (Confirm-Action "Install nrfutil sdk-manager command?" $true) {
                    $installed = Install-NrfUtilSdkManager $nrfUtilInfo.Path
                    if (-not $installed) {
                        Write-Warning "Could not install nrfutil sdk-manager command."
                        $msg = "Toolchain installation will be skipped. " +
                            "Install it manually later."
                        Write-Warning $msg
                        $nrfUtilInfo = $null
                    }
                    else {
                        $nrfUtilInfo.NeedsSdkManagerInstall = $false
                    }
                }
                else {
                    $msg = "Skipping sdk-manager installation. " +
                           "Toolchain will need to be installed manually."
                    Write-Warning $msg
                    $nrfUtilInfo = $null
                }
            }
            else {
                Write-Success "Found nrfutil with sdk-manager at: $($nrfUtilInfo.Path)"
            }
        }
        else {
            Write-Success "Found nrfutil-sdk-manager at: $($nrfUtilInfo.Path)"
        }
    }
    else {
        $msg = "nrfutil not found on PATH and nrfutil-sdk-manager not found " +
               "in VS Code/Cursor extensions."
        Write-Warning $msg
        Write-Host ""
        Write-Info "The nRF Connect SDK toolchain will need to be installed manually."
        $url = "https://www.nordicsemi.com/Products/Development-tools/nRF-Util"
        Write-Info "You can install nrfutil from: $url"
        Write-Info "Or install the nRF Connect for VS Code extension pack."
        Write-Host ""
    }

    # Check if nRF Connect SDK toolchain is installed and prompt for installation
    if ($nrfUtilInfo -and $ncsVersion) {
        $toolchainInstalled = Test-NcsToolchainInstalled `
            -NrfUtilPath $nrfUtilInfo.Path `
            -NrfUtilType $nrfUtilInfo.Type `
            -NcsVersion $ncsVersion

        if ($toolchainInstalled) {
            $msg = "nRF Connect SDK toolchain for $ncsVersion is already installed"
            Write-Success $msg
        }
        else {
            $msg = "nRF Connect SDK toolchain for $ncsVersion is not installed."
            Write-Warning $msg
            Write-Host ""
            Write-Info "The nRF Connect SDK toolchain installation modifies your system."
            Write-Info "The installation may take several minutes."
            Write-Host ""
            
            $confirmMsg = "Install nRF Connect SDK toolchain for $($ncsVersion)?"
            if (Confirm-Action $confirmMsg $true) {
                $installNcsToolchain = $true
            }
            else {
                $msg = "Skipping toolchain installation. " +
                       "You can install it manually later with:"
                Write-Warning $msg
                $cmd = "nrfutil sdk-manager toolchain install " +
                       "--ncs-version $ncsVersion"
                Write-Host "    $cmd" -ForegroundColor DarkGray
            }
        }
    }
    elseif (-not $ncsVersion) {
        Write-Warning "nRF Connect SDK version not specified in board configuration."
        Write-Info "Toolchain installation will need to be done manually."
    }
}
else {
    Write-Step "Checking Zephyr SDK installation..."

    $sdkInstalled = $false
    $toolchainInstalled = $false

    $sdkLocations = if ($isWindowsOS) {
        @(
            "$env:USERPROFILE\zephyr-sdk-$requiredSdkVersion",
            "$env:USERPROFILE\.local\zephyr-sdk-$requiredSdkVersion",
            "C:\zephyr-sdk-$requiredSdkVersion",
            "$env:LOCALAPPDATA\zephyr-sdk-$requiredSdkVersion"
        )
    }
    else {
        @(
            "$HOME/zephyr-sdk-$requiredSdkVersion",
            "$HOME/.local/zephyr-sdk-$requiredSdkVersion",
            "/opt/zephyr-sdk-$requiredSdkVersion",
            "/usr/local/zephyr-sdk-$requiredSdkVersion"
        )
    }

    if ($env:ZEPHYR_SDK_INSTALL_DIR) {
        $additionalLocations = @(
            $env:ZEPHYR_SDK_INSTALL_DIR,
            "$env:ZEPHYR_SDK_INSTALL_DIR\zephyr-sdk-$requiredSdkVersion"
        )
        $sdkLocations = $additionalLocations + $sdkLocations
    }

    foreach ($sdkPath in $sdkLocations) {
        if (Test-Path "$sdkPath\sdk_version") {
            Write-Success "Found Zephyr SDK at: $sdkPath"
            $sdkInstalled = $true
            
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
                $toolchainInstalled = $true
            }
            break
        }
    }

    if (-not $sdkInstalled -or -not $toolchainInstalled) {
        $warningMsg = "Zephyr SDK $requiredSdkVersion"
        if ($requiredToolchain) {
            $warningMsg += " with toolchain '$requiredToolchain'"
        }
        $warningMsg += " is not installed."
        Write-Warning $warningMsg
        Write-Host ""
        Write-Info "The Zephyr SDK will be installed to your user profile directory."
        Write-Info "This is a one-time setup that modifies your system."
        Write-Host ""

        $confirmMsg = "Install Zephyr SDK $requiredSdkVersion"
        if ($requiredToolchain) {
            $confirmMsg += " with '$requiredToolchain' toolchain"
        }
        $confirmMsg += "?"
        if (Confirm-Action $confirmMsg $true) {
            $installSdk = $true
        }
        else {
            $msg = "Skipping SDK installation. " +
                   "You can install it manually later with:"
            Write-Warning $msg
            $manualCmd = "west sdk install --version $requiredSdkVersion"
            if ($requiredToolchain) {
                $manualCmd += " --toolchains $requiredToolchain"
            }
            Write-Host "    $manualCmd" -ForegroundColor DarkGray
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
    $westInitArgs = @("init", "--manifest-url", $ManifestBaseUrl, 
                      "--manifest-file", $manifestFile, ".")
    if ($spotflowRevision) {
        $westInitArgs += "--clone-opt=--revision=$spotflowRevision"
    }

    $westInitOutput = & west @westInitArgs 2>&1
    if ($LASTEXITCODE -ne 0) {
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

# Step 13: Install SDK/toolchain
if ($installNcsToolchain -and $nrfUtilInfo -and $ncsVersion) {
    Write-Step "Installing nRF Connect SDK toolchain for $ncsVersion..."

    $toolchainCmd = $nrfUtilInfo.Path
    $toolchainArgs = if ($nrfUtilInfo.Type -eq "nrfutil") {
        @("sdk-manager", "toolchain", "install", "--ncs-version", $ncsVersion)
    }
    else {
        @("toolchain", "install", "--ncs-version", $ncsVersion)
    }
    
    $displayCmd = if ($nrfUtilInfo.Type -eq "nrfutil") {
        "nrfutil sdk-manager toolchain install --ncs-version $ncsVersion"
    }
    else {
        "nrfutil-sdk-manager toolchain install --ncs-version $ncsVersion"
    }

    Write-Info "Running: $displayCmd"

    try {
        & $toolchainCmd @toolchainArgs
        if ($LASTEXITCODE -ne 0) {
            throw "Toolchain install failed with exit code $LASTEXITCODE"
        }
        $msg = "nRF Connect SDK toolchain installed for version $ncsVersion"
        Write-Success $msg
    }
    catch {
        $msg = "Failed to install nRF Connect SDK toolchain: " +
               "$($_.Exception.Message)"
        Write-ErrorMessage $msg
        Write-Warning "You can try installing it manually with: $displayCmd"
    }
}
elseif ($installSdk) {
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
        $errorMsg = "Failed to install Zephyr SDK: $($_.Exception.Message)"
        Write-ErrorMessage $errorMsg
        $manualCmd = "west sdk install --version $requiredSdkVersion"
        if ($requiredToolchain) {
            $manualCmd += " --toolchains $requiredToolchain"
        }
        Write-Warning "You can try installing it manually later with: $manualCmd"
    }
}

# Summary
Write-Host ""
Write-Host "╔══════════════════════════════════════════════════════════════╗" -ForegroundColor Green
Write-Host "║                    Setup Complete!                           ║" -ForegroundColor Green
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
$configPath = "$($boardConfig.spotflow_path)/zephyr/samples/logs/prj.conf"
Write-Host "  1. Open " -NoNewline
Write-Host $configPath -NoNewline -ForegroundColor DarkGray
Write-Host " and add the required configuration options."
Write-Host ""
Write-Host "  2. Build and flash the Spotflow sample:"
$samplePath = "$($boardConfig.spotflow_path)/zephyr/samples/logs"
Write-Host "     cd $samplePath" -ForegroundColor DarkGray
$buildCmd = "west build --pristine --board $($boardConfig.board)"
if ($boardConfig.build_extra_args) {
    $buildCmd += " $($boardConfig.build_extra_args)"
}
Write-Host "     $buildCmd" -ForegroundColor DarkGray
Write-Host "     west flash" -ForegroundColor DarkGray
Write-Host ""
Write-Host "For more information, visit: " -NoNewline
$quickstartUrlSuffix = if ($zephyr) { "zephyr" } else { "nordic-nrf-connect" }
Write-Host "https://docs.spotflow.io/quickstart/$quickstartUrlSuffix" -ForegroundColor Cyan
Write-Host ""
