param(
    [ValidateSet('x86','x64','arm64')][string[]]$Arch = @('x64'),
    # Local SDL3 source tree; empty fetches the tag pinned in CMakeLists.txt.
    [string]$SdlSource = '',
    [switch]$Test,
    [switch]$Package,
    # Also build the 32-bit reference harnesses (nesticle-test.exe) with MinGW.
    [switch]$TestHooks,
    [string]$MingwRoot = 'C:\msys64\mingw32'
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$common = @("-DFETCHCONTENT_BASE_DIR=$root\build\_deps")
if ($SdlSource) { $common += "-DNESTICLE_SDL_SOURCE=$((Resolve-Path -LiteralPath $SdlSource).Path)" }
function Invoke-Step([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program $($Arguments -join ' ') failed ($LASTEXITCODE)" }
}
$platforms = @{ x86 = 'Win32'; x64 = 'x64'; arm64 = 'ARM64' }
$native = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' }
$built = @()
foreach ($a in $Arch) {
    $dir = Join-Path $root "build\msvc-$a"
    Invoke-Step cmake (@('-S',$root,'-B',$dir,'-A',$platforms[$a]) + $common)
    Invoke-Step cmake @('--build',$dir,'--config','Release','--parallel')
    $exe = Join-Path $dir 'Release\nesticle.exe'
    $built += $exe
    # x64 Windows runs x86 and x64 builds; ARM64 Windows runs all three.
    $runnable = $a -eq 'x86' -or $a -eq $native
    if ($Test -and $runnable) {
        Invoke-Step $exe @('--self-test')
        foreach ($t in 'smoke_test','nes2_test','mapper_test','release_test','netplay_test') {
            Invoke-Step python @((Join-Path $root "tools\$t.py"), $exe)
        }
    } elseif ($Test) { Write-Warning "Skipping tests for $a on this $native host." }
    if ($Package) { Invoke-Step python @((Join-Path $root 'tools\package.py'), $exe, "windows-$a", (Join-Path $root 'dist')) }
}
# bin\ holds the first architecture for launch.cmd and launch-demo.cmd.
$bin = Join-Path $root 'bin'
New-Item -ItemType Directory -Force -Path $bin | Out-Null
foreach ($f in 'nesticle.exe','gui.vol','NESticle.pal','SDL-LICENSE.txt') { Copy-Item -LiteralPath (Join-Path (Split-Path $built[0]) $f) -Destination $bin }
Invoke-Step python @((Join-Path $root 'tools\make_demo.py'), (Join-Path $bin 'demo.nes'))
if ($TestHooks) {
    $saved = $env:PATH
    try {
        $env:PATH = (Join-Path $MingwRoot 'bin') + ';' + $saved
        $dir = Join-Path $root 'build\app'
        Invoke-Step cmake (@('-S',$root,'-B',$dir,'-G','MinGW Makefiles','-DCMAKE_BUILD_TYPE=Release','-DNESTICLE_TEST_HOOKS=ON') + $common)
        Invoke-Step cmake @('--build',$dir,'--parallel')
    } finally { $env:PATH = $saved }
}
Write-Host "Built: $($built -join ', ')"
