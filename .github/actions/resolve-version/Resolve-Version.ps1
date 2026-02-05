[CmdletBinding(DefaultParameterSetName = "Default")]
param(
    [Parameter(Mandatory = $true, ParameterSetName = "GH")]
    [string] $GhRunNumber,
    
    [Parameter(Mandatory = $true, ParameterSetName = "GH")]
    [string] $GhRunAttempt,
    
    [Parameter(Mandatory = $true, ParameterSetName = "GH")]
    [string] $GhEventName,
    
    [Parameter()]
    [string] $GitVersionConfigPath,

    [Parameter()]
    [string] $TagPrefix
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

if(!$GitVersionConfigPath)
{
    $GitVersionConfigPath = [System.IO.Path]::Combine($PSScriptRoot, "GitVersion.yml")
}

if($TagPrefix)
{
    "`ntag-prefix: '$($TagPrefix)'" >> $GitVersionConfigPath
}

Get-Content $GitVersionConfigPath

dotnet-gitversion -config "$GitVersionConfigPath" -showconfig

$rawVersion = dotnet-gitversion -config "$GitVersionConfigPath" | ConvertFrom-Json

Write-Output $rawVersion

$resolvedVersion = ""

if($rawVersion.BranchName -eq "main" -or $rawVersion.BranchName.StartsWith("tags/"))
{
    $resolvedVersion = $rawVersion.SemVer
}
else
{
    $semVer = $rawVersion.SemVer
    $resolvedVersion = "$semVer.$GhRunNumber.$GhRunAttempt"
}

if($env:CI)
{
    Write-Output "version=$resolvedVersion" >> $env:GITHUB_OUTPUT
    Write-Output "Version = $resolvedVersion"
}

Write-Output "Resolved version: $resolvedVersion"
