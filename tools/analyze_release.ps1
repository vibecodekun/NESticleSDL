param(
    [string]$Release = 'nestc042',
    [string]$Ghidra = $env:GHIDRA_INSTALL_DIR,
    [switch]$ProcessOnly,
    [string[]]$Functions = @()
)
$ErrorActionPreference = 'Stop'
if (!$Ghidra) { throw 'Pass -Ghidra <install dir> or set GHIDRA_INSTALL_DIR.' }
$recovery = Join-Path $PSScriptRoot '..\recovery'
$recovery = [IO.Path]::GetFullPath($recovery)
$binary = Join-Path $recovery "releases\$Release\NESTCL95.EXE"
if (!(Test-Path -LiteralPath $binary)) { throw "Inventory the archives first; missing $binary" }
$mapPath = Join-Path $PSScriptRoot "ghidra\$Release-functions.json"
if (!$Functions.Count -and (Test-Path -LiteralPath $mapPath)) {
    $mapping = Get-Content -LiteralPath $mapPath -Raw | ConvertFrom-Json
    $hash = Get-FileHash -LiteralPath $binary -Algorithm SHA256
    if ($hash.Hash.ToLowerInvariant() -ne $mapping.sha256) { throw 'Binary hash does not match recovered function addresses.' }
    $Functions = @($mapping.functions)
}
foreach ($directory in @('projects','settings','cache','temp','exports')) {
    New-Item -ItemType Directory -Force -Path (Join-Path $recovery $directory) | Out-Null
}
# The same Java entry point used by support/analyzeHeadless.bat, with all
# project, settings, cache, and log output confined to this workspace.
$arguments = @(
    '-Xmx2G','-Xshare:off','--enable-native-access=ALL-UNNAMED',
    '-Djava.system.class.loader=ghidra.GhidraClassLoader','-Dfile.encoding=UTF8',
    '-Duser.language=en','-Duser.country=US',
    "-Dapplication.settingsdir=$recovery\settings",
    "-Dapplication.cachedir=$recovery\cache",
    "-Dapplication.tempdir=$recovery\temp",
    '-cp', (Join-Path $Ghidra 'Ghidra\Framework\Utility\lib\Utility.jar'),
    'ghidra.Ghidra','ghidra.app.util.headless.AnalyzeHeadless',
    (Join-Path $recovery 'projects'),$Release,
    '-analysisTimeoutPerFile','180','-max-cpu','4',
    '-scriptPath',(Join-Path $PSScriptRoot 'ghidra'),
    '-log',(Join-Path $recovery "$Release-analysis.log"),
    '-scriptlog',(Join-Path $recovery "$Release-script.log")
)
$arguments += @('-postScript','ExportNESticle.java',(Join-Path $recovery "exports\$Release")) + $Functions
if ($ProcessOnly) { $arguments += @('-process','NESTCL95.EXE','-noanalysis') }
else { $arguments += @('-import',$binary,'-overwrite') }
$startedAt = [DateTime]::UtcNow
& java @arguments
if ($LASTEXITCODE -ne 0) { throw "Ghidra failed ($LASTEXITCODE)." }
$completion = Join-Path $recovery "exports\$Release\NESTCL95.EXE\export.complete"
if (!(Test-Path -LiteralPath $completion) -or (Get-Item -LiteralPath $completion).LastWriteTimeUtc -lt $startedAt) {
    throw 'Ghidra did not complete the export; inspect the analysis/script logs.'
}
