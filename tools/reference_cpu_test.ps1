param([string]$Reference="$PSScriptRoot\..\recovery\releases\nestc042\NESTCL95.EXE",[string[]]$VideoFixtures=@())
$ErrorActionPreference='Stop'
if ((Get-FileHash -LiteralPath $Reference -Algorithm SHA256).Hash.ToLower() -ne '759b0cd77170ce16b5b370d2072626f095410333c2e47d4b364002cc974c31c2') {throw 'Wrong reference executable'}
$root=Split-Path $PSScriptRoot -Parent
$referenceSavedPath=$env:PATH
try {
$env:PATH='C:\msys64\mingw32\bin;'+$env:PATH
$out=Join-Path $root 'build\reference-cpu-test.exe'
& 'C:\msys64\mingw32\bin\g++.exe' -std=c++17 -O2 -static -funsigned-char -Wno-write-strings -Wno-narrowing "-I$root\src" "-I$root\src\m6502" "$PSScriptRoot\reference_cpu_test.cpp" "$root\src\cpu_bridge.cpp" "$root\src\nessound.cpp" "$root\src\release_video.cpp" "$root\src\patch.cpp" "$root\src\disasm.cpp" "$root\src\net_codec.cpp" "$root\src\n6502.cpp" -o $out
if ($LASTEXITCODE) {throw 'Reference test build failed'}
$comparison=@('Reference SHA-256: 759b0cd77170ce16b5b370d2072626f095410333c2e47d4b364002cc974c31c2')+@(& $out $Reference @VideoFixtures)
$comparisonExit=$LASTEXITCODE
$comparison | Set-Content -LiteralPath (Join-Path $root 'build\reference-validation.txt') -Encoding utf8
$comparison | Write-Output
if ($comparisonExit) {throw 'Reference comparison failed'}
} finally { $env:PATH=$referenceSavedPath }
