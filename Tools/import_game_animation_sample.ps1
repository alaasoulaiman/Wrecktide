# import_game_animation_sample.ps1 (2026-10-14): copy the Game Animation Sample files this project
# uses into Content, from your own copy of the sample.
#
# The sample is free on Fab but its licence does not allow the files in a public repository, so the
# repository leaves them out and this script brings them back on your machine. It copies only the
# paths listed in Tools/game_animation_sample_files.txt (folders and single files, relative to the
# Content folder), never overwrites a file that already exists, and never deletes anything.
#
# Usage, from any folder:
#   powershell -ExecutionPolicy Bypass -File Tools/import_game_animation_sample.ps1 -Source "<sample project>/Content" -DryRun
#   powershell -ExecutionPolicy Bypass -File Tools/import_game_animation_sample.ps1 -Source "<sample project>/Content"
#
# -Source  the Content folder of a Game Animation Sample project you created from Fab (the project
#          folder itself is accepted too, the script then uses its Content folder).
# -DryRun  list what would be copied and what already exists, copy nothing.
#
# Exit codes: 0 every listed path was found in the source, 1 some listed paths are missing from the
# source (the rest is still copied), 2 bad arguments or a bad list entry.

param(
    [Parameter(Mandatory = $true)][string]$Source,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$destRoot = Join-Path $repoRoot 'Content'
$listFile = Join-Path $PSScriptRoot 'game_animation_sample_files.txt'

if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
    Write-Output "ERROR: -Source is not a folder: $Source"
    exit 2
}
$srcRoot = (Resolve-Path -LiteralPath $Source).ProviderPath
if (-not (Test-Path -LiteralPath (Join-Path $srcRoot 'Characters') -PathType Container) -and
    (Test-Path -LiteralPath (Join-Path $srcRoot 'Content\Characters') -PathType Container)) {
    $srcRoot = Join-Path $srcRoot 'Content'
    Write-Output "Using the Content folder of the project you named: $srcRoot"
}
if (-not (Test-Path -LiteralPath $destRoot -PathType Container)) {
    Write-Output "ERROR: no Content folder next to Tools: $destRoot"
    exit 2
}
$destFull = (Resolve-Path -LiteralPath $destRoot).ProviderPath.TrimEnd('\', '/')
$srcFull = $srcRoot.TrimEnd('\', '/')
if ($srcFull -ieq $destFull -or $srcFull.StartsWith($destFull + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
    Write-Output "ERROR: -Source points into this project's own Content folder: $srcFull"
    exit 2
}
if (-not (Test-Path -LiteralPath $listFile -PathType Leaf)) {
    Write-Output "ERROR: list file not found: $listFile"
    exit 2
}

# Read and check the list: relative paths only, no parent steps, forward slashes.
$entries = @()
$lineNo = 0
foreach ($raw in Get-Content -LiteralPath $listFile) {
    $lineNo++
    $line = $raw.Trim()
    if ($line -eq '' -or $line.StartsWith('#')) { continue }
    if ($line.Contains('\') -or $line.StartsWith('/') -or $line.Contains(':') -or ($line -split '/' -contains '..')) {
        Write-Output "ERROR: bad entry on line ${lineNo} of the list: $line"
        exit 2
    }
    $entries += $line
}

$plan = New-Object System.Collections.Generic.List[object]
$missing = New-Object System.Collections.Generic.List[string]
foreach ($entry in $entries) {
    $isFolder = $entry.EndsWith('/')
    $rel = $entry.TrimEnd('/').Replace('/', [System.IO.Path]::DirectorySeparatorChar)
    $src = Join-Path $srcRoot $rel
    if ($isFolder) {
        if (-not (Test-Path -LiteralPath $src -PathType Container)) { $missing.Add($entry); continue }
        $srcBase = (Resolve-Path -LiteralPath $src).ProviderPath.TrimEnd('\', '/')
        foreach ($f in Get-ChildItem -LiteralPath $srcBase -Recurse -File) {
            $sub = $f.FullName.Substring($srcBase.Length).TrimStart('\', '/')
            $plan.Add([pscustomobject]@{ Src = $f.FullName; Rel = (Join-Path $rel $sub) })
        }
    }
    else {
        if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { $missing.Add($entry); continue }
        $plan.Add([pscustomobject]@{ Src = (Resolve-Path -LiteralPath $src).ProviderPath; Rel = $rel })
    }
}

$toCopy = New-Object System.Collections.Generic.List[object]
$kept = 0
foreach ($item in $plan) {
    $dst = Join-Path $destRoot $item.Rel
    if (Test-Path -LiteralPath $dst) {
        $kept++
        if ($DryRun) { Write-Output "EXISTS, KEPT  Content/$($item.Rel.Replace('\', '/'))" }
        continue
    }
    $toCopy.Add([pscustomobject]@{ Src = $item.Src; Dst = $dst; Rel = $item.Rel })
}

$copied = 0
foreach ($item in $toCopy) {
    if ($DryRun) {
        Write-Output "WOULD COPY    Content/$($item.Rel.Replace('\', '/'))"
        continue
    }
    $parent = Split-Path -Parent $item.Dst
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    Copy-Item -LiteralPath $item.Src -Destination $item.Dst
    $copied++
}

foreach ($m in $missing) { Write-Output "MISSING IN THE SOURCE  $m" }
$mode = if ($DryRun) { 'DRY RUN, nothing copied' } else { 'copied' }
Write-Output ("Listed paths: {0}. Files found in the source: {1}. To copy: {2}. Already present and kept: {3}. Missing in the source: {4}. Mode: {5}." -f $entries.Count, $plan.Count, $toCopy.Count, $kept, $missing.Count, $mode)
if (-not $DryRun) { Write-Output "Files copied: $copied" }
if ($missing.Count -gt 0) { exit 1 }
exit 0
