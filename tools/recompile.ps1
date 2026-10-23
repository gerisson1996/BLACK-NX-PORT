# Builds the recompiler (XboxXBE-Recomp, clang-cl with the MSVC tools) and
# recompiles Black-Android\assets\default.xbe -- the US image, made from the
# Europe xbe by tools\europe_to_us.py -- into Black-Android\generated\game.
# Steps 3 and 4 of Black-Android\build.bat, without the Android SDK checks.
#
# - The SDK is pinned at 99078b1, main when Black-Android v1.5.0 was released:
#   later commits want "[xbe] library = ..." in game.toml.
# - xbe_recomp opens its inputs with narrow paths and "Área de Trabalho" does
#   not survive that, so it runs through C:\bnx2, a junction to black-nx2.
# - The tools build dir keeps the real path: devkitPro's git cannot update
#   FetchContent submodules through the junction.
$ErrorActionPreference = 'Stop'
$vs = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$real = Join-Path (Split-Path -Parent $PSScriptRoot) 'Black-Android'
if (-not (Test-Path C:\bnx2)) {
    New-Item -ItemType Junction -Path C:\bnx2 -Target (Split-Path -Parent $PSScriptRoot) | Out-Null
}
$env:REAL = $real
$cmake = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake"
$clang = "$vs\VC\Tools\Llvm\x64\bin\clang-cl.exe"
$cmd = "call `"$vs\Common7\Tools\VsDevCmd.bat`" -arch=amd64 -host_arch=amd64 >nul" +
       " && set PATH=$cmake\CMake\bin;$cmake\Ninja;%PATH%" +
       " && cmake -S `"%REAL%\tools`" -B `"%REAL%\out\build\tools`" -G Ninja -DCMAKE_BUILD_TYPE=Release" +
       " `"-DCMAKE_C_COMPILER=$clang`" `"-DCMAKE_CXX_COMPILER=$clang`"" +
       " && cmake --build `"%REAL%\out\build\tools`"" +
       " && cd /d C:\bnx2\Black-Android" +
       " && `"%REAL%\out\build\tools\xbe_recomp.exe`" config\game.toml --xdk C:\bnx2\XboxXBE-Recomp\xdk"
& cmd.exe /c $cmd
if ($LASTEXITCODE -ne 0) { throw "recompile failed ($LASTEXITCODE)" }
Write-Host "generated: $real\generated\game"
