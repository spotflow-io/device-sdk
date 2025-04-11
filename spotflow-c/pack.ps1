param ([string] $Os, [string] $TargetDir = "../target/release")

Push-Location $PSScriptRoot

$packageDir = "$TargetDir/c"
$includeDir = "$packageDir/include"
$binDir = "$packageDir/bin"
$libDir = "$packageDir/lib"
$examplesDir = "$packageDir/examples"

New-Item -Path $packageDir -ItemType Directory
New-Item -Path $includeDir -ItemType Directory
New-Item -Path $binDir -ItemType Directory
New-Item -Path $libDir -ItemType Directory
New-Item -Path $examplesDir -ItemType Directory

Copy-Item "./include/spotflow.h" $includeDir
Copy-Item "./packaging/get_started.c" $examplesDir
Copy-Item "./CHANGELOG.md" $packageDir

if ($Os -Eq "Windows") {
    Copy-Item "$TargetDir/spotflow.dll" $binDir
    Copy-Item "$TargetDir/spotflow.dll.lib" $libDir
    Copy-Item "$TargetDir/spotflow.lib" $libDir
    Copy-Item "./packaging/projects_x64/vs2022_*" $examplesDir -Recurse
} elseif ($Os -Eq "macOS") {
    Copy-Item "$TargetDir/libspotflow.dylib" $binDir
    Copy-Item "$TargetDir/libspotflow.a" $libDir
    Copy-Item "./packaging/projects/clang_*" $examplesDir -Recurse
} elseif ($Os -Eq "Linux") {
    Copy-Item "$TargetDir/libspotflow.so" $binDir
    Copy-Item "$TargetDir/libspotflow.a" $libDir
    Copy-Item "./packaging/projects/gcc_*" $examplesDir -Recurse
} else {
    Write-Error "Unknown OS: $Os"
}

Pop-Location
