param(
    [string]$SourceDir = ".source-lyra",
    [switch]$Refresh
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$sourcePath = Join-Path $projectRoot $SourceDir
$generatedPath = Join-Path $projectRoot "generated"
$buildPath = Join-Path $projectRoot "build"

function Find-Tool([string[]]$Candidates) {
    foreach ($candidate in $Candidates) {
        if (Test-Path $candidate) { return (Resolve-Path $candidate).Path }
        $command = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($command) { return $command.Source }
    }
    throw "Required tool was not found: $($Candidates -join ', ')"
}

$gxx = Find-Tool @("g++.exe", "C:\msys64\mingw64\bin\g++.exe")
$windres = Find-Tool @("windres.exe", "C:\msys64\mingw64\bin\windres.exe")

if (-not (Test-Path (Join-Path $sourcePath "CMakeLists.txt"))) {
    git clone --depth 1 https://github.com/tomokuroki/lyra.git $sourcePath
    if ($LASTEXITCODE -ne 0) { throw "Unable to download the Lyra repository." }
} elseif ($Refresh) {
    git -C $sourcePath pull --ff-only
    if ($LASTEXITCODE -ne 0) { throw "Unable to update the Lyra repository." }
}

New-Item -ItemType Directory -Force $generatedPath, $buildPath | Out-Null

$lyraSources = @(
    "src\main.cpp",
    "src\parser.cpp",
    "src\synth.cpp",
    "src\export.cpp",
    "src\frontend.cpp"
) | ForEach-Object { Join-Path $sourcePath $_ }
$lyraExe = Join-Path $buildPath "lyra.exe"
& $gxx -std=c++17 -O2 -s -static -static-libgcc -static-libstdc++ @lyraSources -o $lyraExe
if ($LASTEXITCODE -ne 0) { throw "Lyra build failed." }

$payload = [System.Collections.Generic.List[object]]::new()
function Add-Payload([string]$File, [string]$RelativePath, [string]$Component) {
    $script:payload.Add([pscustomobject]@{
        File = (Resolve-Path $File).Path
        RelativePath = $RelativePath.Replace('/', '\')
        Component = $Component
    })
}

Add-Payload $lyraExe "lyra.exe" "Core"
Add-Payload (Join-Path $sourcePath "LICENSE") "LICENSE.txt" "Core"
Add-Payload (Join-Path $projectRoot "payload\README.md") "README.md" "Core"

foreach ($item in Get-ChildItem (Join-Path $sourcePath "stdlib") -Recurse -File | Sort-Object FullName) {
    $relative = $item.FullName.Substring($sourcePath.Length + 1)
    Add-Payload $item.FullName $relative "Stdlib"
}
foreach ($item in Get-ChildItem (Join-Path $sourcePath "docs") -Recurse -File | Sort-Object FullName) {
    $relative = $item.FullName.Substring($sourcePath.Length + 1)
    if ($relative -like "docs\ru\*" -or $item.Name -like "*_RU.md") { continue }
    Add-Payload $item.FullName $relative "Docs"
}
foreach ($item in Get-ChildItem (Join-Path $sourcePath "examples") -Recurse -File | Sort-Object FullName) {
    $relative = $item.FullName.Substring($sourcePath.Length + 1)
    Add-Payload $item.FullName $relative "Examples"
}
foreach ($name in @("CMakeLists.txt", "Makefile", "CONTRIBUTING.md")) {
    Add-Payload (Join-Path $sourcePath $name) ("source\" + $name) "Source"
}
foreach ($item in Get-ChildItem (Join-Path $sourcePath "src") -Recurse -File | Sort-Object FullName) {
    $relative = "source\" + $item.FullName.Substring($sourcePath.Length + 1)
    Add-Payload $item.FullName $relative "Source"
}

$rcLines = [System.Collections.Generic.List[string]]::new()
$manifestLines = [System.Collections.Generic.List[string]]::new()
$resourceId = 1000
foreach ($item in $payload) {
    $escapedFile = $item.File.Replace('\', '\\')
    $escapedRelative = $item.RelativePath.Replace('\', '\\').Replace('"', '\"')
    $rcLines.Add("$resourceId RCDATA `"$escapedFile`"")
    $manifestLines.Add("    {$resourceId, L`"$escapedRelative`", Component::$($item.Component)},")
    $resourceId++
}
[IO.File]::WriteAllLines((Join-Path $generatedPath "payload.rc"), $rcLines, [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllLines((Join-Path $generatedPath "payload_manifest.inc"), $manifestLines, [Text.UTF8Encoding]::new($false))

$resourceObject = Join-Path $buildPath "installer-res.o"
& $windres (Join-Path $projectRoot "installer.rc") -O coff -o $resourceObject
if ($LASTEXITCODE -ne 0) { throw "Installer resource build failed." }

$output = Join-Path $projectRoot "lyra-1.0.0.exe"
& $gxx -std=c++17 -O2 -s -static -static-libgcc -static-libstdc++ -municode -mwindows `
    (Join-Path $projectRoot "installer.cpp") $resourceObject -o $output `
    -lcomctl32 -lshell32 -lole32 -ladvapi32 -luuid -lgdiplus
if ($LASTEXITCODE -ne 0) { throw "Installer build failed." }

Write-Host "Done: $output" -ForegroundColor Green
& $lyraExe --version
Get-Item $output | Select-Object FullName, Length, LastWriteTime

