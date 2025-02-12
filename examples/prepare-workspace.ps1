param ([string] $WorkspaceId, [string] $Instance = "api.eu1.spotflow.io")

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true

spotf login --workspace $WorkspaceId --instance-uri $Instance --output json
if ($LastExitCode -ne 0) {
    Write-Output "Failed to log in. Exiting..."
    exit $LastExitCode
}

$tokenName = "device-sdk-test-token"

Write-Output "Searching for a Provisioning Token with name '$tokenName'..."
$tokens = spotf provisioning-token list --output json | ConvertFrom-Json | Where-Object { $_.displayName -eq $tokenName }
Write-Output "Done."

if ($tokens.Count -eq 0) {
    Write-Host "Creating a new Provisioning Token..."
    $token = spotf provisioning-token create --display-name $tokenName --output json | ConvertFrom-Json
    Write-Output "Done."
} else {
    Write-Host "Found existing Provisioning Token, getting its value..."
    $token = spotf provisioning-token regenerate --provisioning-token-id $tokens[0].provisioningTokenId --output json | ConvertFrom-Json
    Write-Output "Done."
}

Write-Output "Setting up data flows..."

$streamGroupName = "device-sdk"

spotf stream-group create-or-update --name $streamGroupName --use-workspace-storage

spotf stream create-or-update --stream-group-name $streamGroupName --stream-name "autofilled-batch" --concatenation-mode WithNewLines --enable-site-id false `
    --with-batch-id-autofill-pattern "{dateTime:yyyy-MM-dd-hh-mm}" --with-msg-id-autofill-pattern "{dateTime:yyyy-MM-dd}_{sequenceId}"

spotf stream create-or-update --stream-group-name $streamGroupName --stream-name "c" --concatenation-mode WithNewLines --enable-site-id false

spotf stream create-or-update --stream-group-name $streamGroupName --stream-name "default" --concatenation-mode None --enable-site-id false `
    --with-batch-id-autofill-pattern "{dateTime:yyyy-MM-dd-hh-mm}" --with-msg-id-autofill-pattern "{sequenceId}"

spotf stream create-or-update --stream-group-name $streamGroupName --stream-name "manual" --concatenation-mode None --enable-site-id false

spotf stream create-or-update --stream-group-name $streamGroupName --stream-name "manual-batch" --concatenation-mode WithNewLines --enable-site-id false

spotf stream create-or-update --stream-group-name $streamGroupName --stream-name "manual-batch-slices" --concatenation-mode WithNewLines --enable-site-id false

spotf stream create-or-update --stream-group-name $streamGroupName --stream-name "rust" --concatenation-mode WithNewLines --enable-site-id false

spotf stream-group create-or-update --name $streamGroupName --default-stream "default"

Write-Output "Done."

Write-Output ""
Write-Output "The Workspace is ready for testing. Set the following environment variables before running the tests:"

Write-Output "SPOTFLOW_DEVICE_SDK_TEST_INSTANCE: $Instance"
Write-Output "SPOTFLOW_DEVICE_SDK_TEST_PROVISIONING_TOKEN: $($token.provisioningToken)"
Write-Output "SPOTFLOW_DEVICE_SDK_TEST_WORKSPACE_ID: $WorkspaceId"
Write-Output "SPOTFLOW_DEVICE_SDK_TEST_API_TOKEN: Set according to this guide: https://docs.spotflow.io/api/#authentication"
