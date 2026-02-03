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
    The Spotflow module revision to checkout. If not specified, the script will use the latest
    revision.

.PARAMETER quickstartJsonUrl
    The URL of the quickstart.json file to use. If not specified, the script will use the default
    URL.

.PARAMETER gitHubToken
    The optional GitHub token to use for authentication during Zephyr SDK installation.

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
    [string]$quickstartJsonUrl = "",
    [string]$gitHubToken = "",
    [switch]$autoConfirm
)

# Configuration
$DefaultQuickstartJsonUrl = "https://downloads.spotflow.io/quickstart.json"
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
    Write-Host "You can also contact us on Discord:" -ForegroundColor Gray
    Write-Host "  https://discord.com/channels/1372202003635114125/1379411086163574864"`
        -ForegroundColor Cyan
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

    # User messages sent to host (stderr equivalent) to split them from the function output
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

# Arguments

function Set-SdkType {
    param(
        [bool]$Zephyr,
        [bool]$Ncs
    )

    if ($Zephyr -and $Ncs) {
        $errorMsg = "Cannot specify both -zephyr and -ncs"
        $details = "Please use only one of these options."
        Exit-WithError $errorMsg $details
    }

    if (-not $Zephyr -and -not $Ncs) {
        $errorMsg = "Must specify either -zephyr or -ncs"
        $details = "Use -zephyr for upstream Zephyr or -ncs for nRF Connect SDK."
        Exit-WithError $errorMsg $details
    }

    $sdkType = if ($Zephyr) { "zephyr" } else { "ncs" }
    $sdkDisplayType = if ($Zephyr) { "Zephyr" } else { "nRF Connect SDK" }

    Write-Info "SDK type: $sdkDisplayType"
    Write-Info "Board ID: $board"

    return @{
        Type        = $sdkType
        DisplayType = $sdkDisplayType
    }
}

# Prerequisites

function Find-Python {
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

    return $pythonCmd
}

function Test-Git {
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
}

# Board configuration

function Get-QuickstartJson {
    param([string]$JsonUrl)

    try {
        $quickstartJson = Invoke-RestMethod -Uri $JsonUrl -UseBasicParsing -ErrorAction Stop
        Write-Success "Configuration downloaded successfully"
        return $quickstartJson
    }
    catch {
        Exit-WithError "Failed to download board configuration" $_.Exception.Message
    }
}

function Find-BoardConfig {
    param(
        [object]$QuickstartJson,
        [string]$SdkType,
        [string]$Board
    )

    $boardConfig = $null
    $vendorName = $null

    if ($SdkType -eq "ncs" -and $QuickstartJson.ncs -and $QuickstartJson.ncs.boards) {
        $boardConfig = $QuickstartJson.ncs.boards |
            Where-Object { $_.id -eq $Board } |
            Select-Object -First 1
    }
    elseif ($SdkType -eq "zephyr" -and $QuickstartJson.zephyr -and
            $QuickstartJson.zephyr.vendors) {
        foreach ($vendor in $QuickstartJson.zephyr.vendors) {
            $found = $vendor.boards |
                Where-Object { $_.id -eq $Board } |
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
        if ($SdkType -eq "ncs" -and $QuickstartJson.ncs -and $QuickstartJson.ncs.boards) {
            $availableBoards = $QuickstartJson.ncs.boards | ForEach-Object { $_.id }
        }
        elseif ($SdkType -eq "zephyr" -and $QuickstartJson.zephyr -and $QuickstartJson.zephyr.vendors) {
            foreach ($vendor in $QuickstartJson.zephyr.vendors) {
                $availableBoards += $vendor.boards | ForEach-Object { $_.id }
            }
        }

        $boardList = ($availableBoards | Where-Object { $_ }) -join ", "
        $errorMsg = "Board '$Board' not found in $sdkDisplayType configuration"
        Exit-WithError $errorMsg "Available boards: $boardList"
    }

    return @{
        Config     = $boardConfig
        VendorName = $vendorName
    }
}

function Write-BoardInfo {
    param(
        [object]$BoardConfig,
        [string]$VendorName
    )

    Write-Success "Found board: $($BoardConfig.name)"
    if ($VendorName) {
        Write-Info "Vendor: $VendorName"
    }
    Write-Info "Connection methods: $($BoardConfig.connection -join ", ")"
    Write-Info "Board target: $($BoardConfig.board)"
    Write-Info "Manifest: $($BoardConfig.manifest)"
    Write-Info "SDK version: $($BoardConfig.sdk_version)"
    if ($BoardConfig.sdk_toolchain) {
        Write-Info "Toolchain: $($BoardConfig.sdk_toolchain)"
    }
    if ($BoardConfig.blob) {
        Write-Info "Blob: $($BoardConfig.blob)"
    }
    if ($BoardConfig.ncs_version) {
        Write-Info "nRF Connect SDK version: $($BoardConfig.ncs_version)"
    }
}

function Write-Callout {
    param([string]$Callout)

    if ($Callout) {
        $calloutText = $Callout -replace '<[^>]+>', ''
        Write-Host ""
        Write-Warning "Note: $calloutText"
    }
}

# Workspace folder

function Get-WorkspaceFolder {
    param(
        [string]$ProvidedFolder,
        [string]$SdkDisplayType,
        [bool]$IsWindowsOS
    )

    $defaultFolder = if ($IsWindowsOS) {
        "C:\spotflow-ws"
    }
    else {
        Join-Path $HOME "spotflow-ws"
    }

    Write-Info "The workspace will contain all $SdkDisplayType files and your project."
    if ($IsWindowsOS) {
        Write-Warning "Tip: Avoid very long paths on Windows to prevent build issues."
    }
    Write-Host ""

    $workspaceFolder = Read-Input "Enter workspace folder path" $defaultFolder -ProvidedValue $ProvidedFolder

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

    return $workspaceFolder
}

function New-WorkspaceFolder {
    param([string]$WorkspaceFolder)

    try {
        if (-not (Test-Path $WorkspaceFolder)) {
            New-Item -ItemType Directory -Path $WorkspaceFolder -Force | Out-Null
            Write-Success "Created folder: $WorkspaceFolder"
        }
        else {
            Write-Info "Folder already exists: $WorkspaceFolder"
        }
    }
    catch {
        Exit-WithError "Failed to create workspace folder" $_.Exception.Message
    }
}

# Workspace initialization

function New-PythonVenv {
    param(
        [string]$WorkspaceFolder,
        [string]$PythonCmd
    )

    $venvPath = Join-Path $WorkspaceFolder ".venv"

    try {
        if (Test-Path $venvPath) {
            Write-Info "Virtual environment already exists, reusing it"
        }
        else {
            & $PythonCmd -m venv .venv
            if ($LASTEXITCODE -ne 0) {
                throw "venv creation failed with exit code $LASTEXITCODE"
            }
            Write-Success "Virtual environment created"
        }
    }
    catch {
        Exit-WithError "Failed to create Python virtual environment" $_.Exception.Message
    }

    return $venvPath
}

function Enable-PythonVenv {
    param([string]$VenvPath)

    $activateScript = Join-Path $VenvPath "Scripts\Activate.ps1"
    if (-not (Test-Path $activateScript)) {
        $activateScript = Join-Path $VenvPath "bin/Activate.ps1"
    }

    try {
        . $activateScript
        Write-Success "Virtual environment activated"
    }
    catch {
        Exit-WithError "Failed to activate virtual environment" $_.Exception.Message
    }
}

function Install-West {
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
}

function Initialize-WestWorkspace {
    param(
        [string]$Manifest,
        [string]$ManifestBaseUrl,
        [string]$SpotflowRevision
    )

    $manifestFile = "zephyr/manifests/$Manifest"

    Write-Info "Manifest URL: $ManifestBaseUrl"
    Write-Info "Manifest file: $manifestFile"

    try {
        $westInitArgs = @("init", "--manifest-url", $ManifestBaseUrl,
                          "--manifest-file", $manifestFile, ".")
        if ($SpotflowRevision) {
            $westInitArgs += "--clone-opt=--revision=$SpotflowRevision"
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
}

function Update-WestWorkspace {
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
}

function Install-PythonPackages {
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
}

function Get-Blobs {
    param([string]$Blob)

    try {
        & west blobs fetch $Blob --auto-accept
        if ($LASTEXITCODE -ne 0) {
            throw "west blobs fetch failed with exit code $LASTEXITCODE"
        }
        Write-Success "Binary blobs fetched"
    }
    catch {
        Exit-WithError "Failed to fetch binary blobs" $_.Exception.Message
    }
}

# Zephyr SDK

function Test-ZephyrSdkInstallation {
    param(
        [string]$SdkVersion,
        [string]$SdkToolchain,
        [bool]$IsWindowsOS
    )

    $sdkInstalled = $false
    $toolchainInstalled = $false
    $installSdk = $false

    $sdkLocations = if ($IsWindowsOS) {
        @(
            "$env:USERPROFILE\zephyr-sdk-$SdkVersion",
            "$env:USERPROFILE\.local\zephyr-sdk-$SdkVersion",
            "C:\zephyr-sdk-$SdkVersion",
            "$env:LOCALAPPDATA\zephyr-sdk-$SdkVersion"
        )
    }
    else {
        @(
            "$HOME/zephyr-sdk-$SdkVersion",
            "$HOME/.local/zephyr-sdk-$SdkVersion",
            "/opt/zephyr-sdk-$SdkVersion",
            "/usr/local/zephyr-sdk-$SdkVersion"
        )
    }

    if ($env:ZEPHYR_SDK_INSTALL_DIR) {
        $additionalLocations = @(
            $env:ZEPHYR_SDK_INSTALL_DIR,
            "$env:ZEPHYR_SDK_INSTALL_DIR\zephyr-sdk-$SdkVersion"
        )
        $sdkLocations = $additionalLocations + $sdkLocations
    }

    foreach ($sdkPath in $sdkLocations) {
        if (Test-Path "$sdkPath\sdk_version") {
            Write-Success "Found Zephyr SDK at: $sdkPath"
            $sdkInstalled = $true

            if ($SdkToolchain) {
                $toolchainPath = Join-Path $sdkPath $SdkToolchain
                if (Test-Path $toolchainPath) {
                    Write-Success "Toolchain '$SdkToolchain' is installed"
                    $toolchainInstalled = $true
                }
                else {
                    Write-Warning "Toolchain '$SdkToolchain' not found in SDK"
                }
            }
            else {
                $toolchainInstalled = $true
            }
            break
        }
    }

    if (-not $sdkInstalled -or -not $toolchainInstalled) {
        $warningMsg = "Zephyr SDK $SdkVersion"
        if ($SdkToolchain) {
            $warningMsg += " with toolchain '$SdkToolchain'"
        }
        $warningMsg += " is not installed."
        Write-Warning $warningMsg
        Write-Host ""
        Write-Info "The Zephyr SDK will be installed to your user profile directory."
        Write-Host ""

        $confirmMsg = "Install Zephyr SDK $SdkVersion"
        if ($SdkToolchain) {
            $confirmMsg += " with '$SdkToolchain' toolchain"
        }
        $confirmMsg += "?"
        if (Confirm-Action $confirmMsg $true) {
            $installSdk = $true
        }
        else {
            $msg = "Skipping SDK installation. " +
                   "You can install it manually later with:"
            Write-Warning $msg
            $manualCmd = "west sdk install --version $SdkVersion"
            if ($SdkToolchain) {
                $manualCmd += " --toolchains $SdkToolchain"
            }
            Write-Host "    $manualCmd" -ForegroundColor DarkGray
        }
    }
    else {
        Write-Success "Zephyr SDK is properly installed"
    }

    return $installSdk
}

function Install-ZephyrSdk {
    param(
        [string]$SdkVersion,
        [string]$SdkToolchain,
        [string]$GitHubToken
    )

    $sdkArgs = @("sdk", "install", "--version", $SdkVersion)
    if ($SdkToolchain) {
        $sdkArgs += @("--toolchains", $SdkToolchain)
    }
    if ($GitHubToken) {
        $sdkArgs += @("--personal-access-token", $GitHubToken)
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
        $manualCmd = "west sdk install --version $SdkVersion"
        if ($SdkToolchain) {
            $manualCmd += " --toolchains $SdkToolchain"
        }
        Write-Warning "You can try installing it manually later with: $manualCmd"
    }
}

# nRF Connect SDK

function Test-NrfUtilHasSdkManager {
    param([string]$NrfUtilPath)

    try {
        $null = & $NrfUtilPath sdk-manager --version 2>&1
        return ($LASTEXITCODE -eq 0)
    }
    catch {
        return $false
    }
}

function Find-NrfUtilSdkManagerInExtensions {
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
    # Returns:
    #   Hashtable with 'Path', 'Type', 'NeedsSdkManagerInstall'
    #   or $null if not found

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

function Test-NcsInstallation {
    param([string]$NcsVersion)

    $nrfUtilInfo = Find-NrfUtil
    $nrfUtilType = $null
    $nrfUtilPath = $null

    if ($nrfUtilInfo) {
        $nrfUtilType = $nrfUtilInfo.Type
        $nrfUtilPath = $nrfUtilInfo.Path

        if ($nrfUtilType -eq "nrfutil") {
            if ($nrfUtilInfo.NeedsSdkManagerInstall) {
                Write-Info "Found nrfutil at: $nrfUtilPath"
                Write-Warning "The sdk-manager command is not installed for nrfutil."
                Write-Host ""
                Write-Info "This command is required to install the nRF Connect SDK toolchain."
                Write-Host ""

                if (Confirm-Action "Install nrfutil sdk-manager command?" $true) {
                    $installed = Install-NrfUtilSdkManager $nrfUtilPath
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
                Write-Success "Found nrfutil with sdk-manager at: $nrfUtilPath"
            }
        }
        else {
            Write-Success "Found nrfutil-sdk-manager at: $nrfUtilPath"
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
    $shouldInstallNcsToolchain = $false
    if ($nrfUtilInfo -and $NcsVersion) {
        $toolchainInstalled = Test-NcsToolchainInstalled `
            -NrfUtilPath $nrfUtilPath `
            -NrfUtilType $nrfUtilType `
            -NcsVersion $NcsVersion

        if ($toolchainInstalled) {
            $msg = "nRF Connect SDK toolchain for $NcsVersion is already installed"
            Write-Success $msg
        }
        else {
            $msg = "nRF Connect SDK toolchain for $NcsVersion is not installed."
            Write-Warning $msg
            Write-Host ""
            Write-Info "The installation may take several minutes."
            Write-Host ""

            $confirmMsg = "Install nRF Connect SDK toolchain for $NcsVersion?"
            if (Confirm-Action $confirmMsg $true) {
                $shouldInstallNcsToolchain = $true
            }
            else {
                $msg = "Skipping toolchain installation. " +
                       "You can install it manually later with:"
                Write-Warning $msg
                $cmd = "nrfutil sdk-manager toolchain install " +
                       "--ncs-version $NcsVersion"
                Write-Host "    $cmd" -ForegroundColor DarkGray
            }
        }
    }
    elseif (-not $NcsVersion) {
        Write-Warning "nRF Connect SDK version not specified in board configuration."
        Write-Info "Toolchain installation will need to be done manually."
    }

    if ($shouldInstallNcsToolchain) {
        return @{
            Type = $nrfUtilType
            Path = $nrfUtilPath
        }
    }

    return $null
}

function Install-NcsToolchain {
    param(
        [string]$NrfUtilType,
        [string]$NrfUtilPath,
        [string]$NcsVersion
    )

    $toolchainCmd = $NrfUtilPath
    $toolchainArgs = if ($NrfUtilType -eq "nrfutil") {
        @("sdk-manager", "toolchain", "install", "--ncs-version", $NcsVersion)
    }
    else {
        @("toolchain", "install", "--ncs-version", $NcsVersion)
    }

    $displayCmd = if ($NrfUtilType -eq "nrfutil") {
        "nrfutil sdk-manager toolchain install --ncs-version $NcsVersion"
    }
    else {
        "nrfutil-sdk-manager toolchain install --ncs-version $NcsVersion"
    }

    Write-Info "Running: $displayCmd"

    try {
        & $toolchainCmd @toolchainArgs
        if ($LASTEXITCODE -ne 0) {
            throw "Toolchain install failed with exit code $LASTEXITCODE"
        }
        $msg = "nRF Connect SDK toolchain installed for version $NcsVersion"
        Write-Success $msg
    }
    catch {
        $msg = "Failed to install nRF Connect SDK toolchain: " +
               "$($_.Exception.Message)"
        Write-ErrorMessage $msg
        Write-Warning "You can try installing it manually with: $displayCmd"
    }
}

# Configuration

function Get-QuickstartUrl {
    param([string]$SdkType)

    $quickstartUrlSuffix = if ($SdkType -eq "zephyr") { "zephyr" } else { "nordic-nrf-connect" }
    return "https://docs.spotflow.io/quickstart/$quickstartUrlSuffix"
}

function Add-ConfigurationPlaceholders {
    param(
        [string]$ConfigPath,
        [object]$BoardConfig,
        [string]$QuickstartUrl
    )

    $configContent = Get-Content $ConfigPath -Raw

    if ($configContent -match "CONFIG_SPOTFLOW_DEVICE_ID") {
        Write-Info "CONFIG_SPOTFLOW_DEVICE_ID already exists in prj.conf, skipping"
        return
    }

    $prependedConfigContent = ""
    if ($BoardConfig.connection -contains "wifi") {
        $prependedConfigContent += "CONFIG_NET_WIFI_SSID=""<Your Wi-Fi SSID>""`n"
        $prependedConfigContent += "CONFIG_NET_WIFI_PASSWORD=""<Your Wi-Fi Password>""`n`n"
    }
    $prependedConfigContent += "CONFIG_SPOTFLOW_DEVICE_ID=""$($BoardConfig.sample_device_id)""`n"
    $prependedConfigContent += "CONFIG_SPOTFLOW_INGEST_KEY=""<Your Spotflow Ingest Key>""`n`n"

    $configContent = $prependedConfigContent + $configContent

    try {
        Set-Content -Path $ConfigPath -Value $configContent
        Write-Success "Configuration placeholders added to prj.conf"
    }
    catch {
        $errorMsg = "Failed to add configuration placeholders to prj.conf: $($_.Exception.Message)"
        Write-ErrorMessage $errorMsg
        Write-Warning `
            "See the quickstart guide for the list of configuration options: $QuickstartUrl"
    }
}

function Main {
    Write-Host ""
    Write-Host "+--------------------------------------------------------------+" -ForegroundColor Cyan
    Write-Host "|            Spotflow Device SDK - Workspace Setup             |" -ForegroundColor Cyan
    Write-Host "+--------------------------------------------------------------+" -ForegroundColor Cyan
    Write-Host ""

    $sdkInfo = Set-SdkType -Zephyr $zephyr -Ncs $ncs
    $sdkType = $sdkInfo.Type
    $sdkDisplayType = $sdkInfo.DisplayType

    Write-Step "Checking prerequisites..."
    $pythonCmd = Find-Python
    Test-Git

    Write-Step "Downloading board configuration..."
    $jsonUrl = if ($quickstartJsonUrl) { $quickstartJsonUrl } else { $DefaultQuickstartJsonUrl }
    $quickstartJson = Get-QuickstartJson -JsonUrl $jsonUrl

    Write-Step "Looking up board '$board'..."
    $boardResult = Find-BoardConfig -QuickstartJson $quickstartJson -SdkType $sdkType -Board $board
    $boardConfig = $boardResult.Config
    $vendorName = $boardResult.VendorName

    Write-BoardInfo -BoardConfig $boardConfig -VendorName $vendorName
    Write-Callout -Callout $boardConfig.callout

    # Detect if running on Windows
    $isWindowsOS = ($env:OS -eq 'Windows_NT') -or ($IsWindows -eq $true)

    Write-Step "Configuring workspace location..."
    $workspaceFolder = Get-WorkspaceFolder `
        -ProvidedFolder $workspaceFolder `
        -SdkDisplayType $sdkDisplayType `
        -IsWindowsOS $isWindowsOS

    $requiredSdkVersion = $boardConfig.sdk_version
    $requiredToolchain = $boardConfig.sdk_toolchain
    $ncsVersion = $boardConfig.ncs_version

    $installSdk = $false
    $nrfUtilInfo = $null

    if ($sdkType -eq "ncs") {
        Write-Step "Checking nRF Connect SDK toolchain setup..."
        $nrfUtilInfo = Test-NcsInstallation -NcsVersion $ncsVersion
    }
    else {
        Write-Step "Checking Zephyr SDK installation..."
        $installSdk = Test-ZephyrSdkInstallation `
            -SdkVersion $requiredSdkVersion `
            -SdkToolchain $requiredToolchain `
            -IsWindowsOS $isWindowsOS
    }

    Write-Step "Creating workspace folder..."
    New-WorkspaceFolder -WorkspaceFolder $workspaceFolder

    # Change to workspace folder
    try {
        Set-Location $workspaceFolder
        Write-Success "Changed to workspace folder"
    }
    catch {
        Exit-WithError "Failed to change to workspace folder" $_.Exception.Message
    }

    Write-Step "Creating Python virtual environment..."
    $venvPath = New-PythonVenv -WorkspaceFolder $workspaceFolder -PythonCmd $pythonCmd

    Write-Step "Activating virtual environment..."
    Enable-PythonVenv -VenvPath $venvPath

    Write-Step "Installing West..."
    Install-West

    Write-Step "Initializing West workspace..."
    Initialize-WestWorkspace `
        -Manifest $boardConfig.manifest `
        -ManifestBaseUrl $ManifestBaseUrl `
        -SpotflowRevision $spotflowRevision

    Write-Step "Downloading dependencies (this may take several minutes)..."
    Update-WestWorkspace

    Write-Step "Installing Python packages..."
    Install-PythonPackages

    if ($boardConfig.blob) {
        Write-Step "Fetching binary blobs for $($boardConfig.blob)..."
        Get-Blobs -Blob $boardConfig.blob
    }

    if ($nrfUtilInfo -and $ncsVersion) {
        Write-Step "Installing nRF Connect SDK toolchain for $ncsVersion..."
        Install-NcsToolchain `
            -NrfUtilType $nrfUtilInfo.Type `
            -NrfUtilPath $nrfUtilInfo.Path `
            -NcsVersion $ncsVersion
    }
    elseif ($installSdk) {
        Write-Step "Installing Zephyr SDK $requiredSdkVersion..."
        Install-ZephyrSdk `
            -SdkVersion $requiredSdkVersion `
            -SdkToolchain $requiredToolchain `
            -GitHubToken $gitHubToken
    }

    $quickstartUrl = Get-QuickstartUrl -SdkType $sdkType

    $samplePath = "$($boardConfig.spotflow_path)/zephyr/samples/logs"
    $configPath = "$samplePath/prj.conf"

    Write-Step "Adding configuration placeholders to prj.conf..."
    Add-ConfigurationPlaceholders `
        -ConfigPath $configPath `
        -BoardConfig $boardConfig `
        -QuickstartUrl $quickstartUrl

    # Summary
    $buildCmd = "west build --pristine --board $($boardConfig.board)"
    if ($boardConfig.build_extra_args) {
        $buildCmd += " $($boardConfig.build_extra_args)"
    }
    Write-Host ""
    Write-Host "+--------------------------------------------------------------+" -ForegroundColor Green
    Write-Host "|                       Setup Complete!                        |" -ForegroundColor Green
    Write-Host "+--------------------------------------------------------------+" -ForegroundColor Green
    Write-Host ""
    Write-Host "Workspace location: " -NoNewline
    Write-Host $workspaceFolder -ForegroundColor Cyan
    Write-Host "Board: " -NoNewline
    Write-Host "$($boardConfig.name) ($($boardConfig.board))" -ForegroundColor Cyan
    Write-Host "Spotflow module path: " -NoNewline
    Write-Host $boardConfig.spotflow_path -ForegroundColor Cyan
    Write-Host ""
    Write-Host "Next steps to finish the quickstart:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "  1. Open " -NoNewline
    Write-Host $configPath -NoNewline -ForegroundColor DarkGray
    Write-Host " and fill in the required configuration options."
    Write-Host ""
    if ($zephyr) {
        Write-Host "  2. Build and flash the Spotflow sample:"
    }
    else {
        Write-Host "  2. In a terminal with the nRF Connect toolchain environment, " -NoNewline
        Write-Host "build and flash the Spotflow sample:"
    }
    Write-Host ""
    Write-Host "     cd $samplePath" -ForegroundColor DarkGray
    Write-Host "     $buildCmd" -ForegroundColor DarkGray
    Write-Host "     west flash" -ForegroundColor DarkGray
    Write-Host ""
    Write-Host "For more information, visit: " -NoNewline
    Write-Host $quickstartUrl -ForegroundColor Cyan
    Write-Host ""
}

Main
