param(
    [string]$Version = '',
    [string]$OutDir = '',
    [string]$Generator = 'Visual Studio 17 2022'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $OutDir) { $OutDir = Join-Path $root 'dist' }
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Invoke-Native([string]$Exe, [string[]]$Arguments) {
    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Exe $($Arguments -join ' ') failed with exit code $LASTEXITCODE." }
}

if (-not $Version) {
    $Version = git -C $root describe --tags --match 'v[0-9]*' --dirty 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $Version) {
        $Version = '0.0.0-g' + (git -C $root rev-parse --short HEAD)
    }
}
$Version = $Version.Trim() -replace '^v', ''
if ($Version -notmatch '^[0-9A-Za-z.+-]+$') { throw "Version '$Version' has characters that are not allowed in a file name." }

function New-Zip([string]$Stage, [string]$Zip) {
    if (Test-Path $Zip) { Remove-Item $Zip }
    [System.IO.Compression.ZipFile]::CreateFromDirectory($Stage, $Zip, [System.IO.Compression.CompressionLevel]::Optimal, $false)
    $archive = [System.IO.Compression.ZipFile]::OpenRead($Zip)
    try { "$Zip : $($archive.Entries.Count) entries, $((Get-Item $Zip).Length) bytes" } finally { $archive.Dispose() }
}

function Assert-Contains([string]$File, [string]$Text) {
    $content = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($File))
    if (-not $content.Contains($Text)) { throw "$File does not contain '$($Text.Trim())': this script did not build it." }
}

$variants = @(
    @{ Suffix = ''; MinPlayers = 4; Title = '' },
    @{ Suffix = '-min2'; MinPlayers = 2; Title = ', two-player lobby build' }
)

New-Item -ItemType Directory -Force $OutDir | Out-Null
foreach ($variant in $variants) {
    $build = Join-Path $root "build\package$($variant.Suffix)"
    $out = Join-Path $build 'out'
    $stage = Join-Path $build 'stage'
    foreach ($folder in $out, $stage) { if (Test-Path $folder) { Remove-Item -Recurse -Force $folder } }

    Invoke-Native cmake @('-S', $root, '-B', $build, '-G', $Generator, '-A', 'Win32',
        "-DBLTD_VERSION=$Version", "-DBLTD_LOBBY_MIN_PLAYERS=$($variant.MinPlayers)", "-DBLTD_OUTPUT_DIR=$out")
    Invoke-Native cmake @('--build', $build, '--config', 'Release')

    $dll = Join-Path $out 'dinput8.dll'
    Assert-Contains $dll "bltd-bots $Version ("
    Assert-Contains $dll "MinPlayers=$($variant.MinPlayers)`r`n"

    New-Item -ItemType Directory -Force (Join-Path $stage 'bltd-bots') | Out-Null
    Copy-Item $dll (Join-Path $stage 'dinput8.dll')
    foreach ($folder in 'navcache', 'ui', 'maps') {
        Copy-Item (Join-Path $out "bltd-bots\$folder") (Join-Path $stage "bltd-bots\$folder") -Recurse
    }
    $readme = (Get-Content -Raw (Join-Path $root 'package\README.txt')).Replace('@VERSION@', $Version)
    $readme = $readme.Replace('@VARIANT@', $variant.Title)
    [System.IO.File]::WriteAllText((Join-Path $stage 'README.txt'), ($readme -replace "`r?`n", "`r`n"))
    New-Zip $stage (Join-Path $OutDir "bltd-enhancements-mod-$Version$($variant.Suffix).zip")
}
