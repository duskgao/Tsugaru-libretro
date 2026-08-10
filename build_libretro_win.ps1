# ============================================================================
#  FMTOWNS (TOWNSEMU) libretro core - Windows one-click build + deploy script
#
#  Rebuilds towns_libretro.dll with MSYS2 MinGW and deploys to RetroArch.
#
#  Usage:
#    powershell -ExecutionPolicy Bypass -File E:\FMTOWNS\build_libretro_win.ps1
#
#  Optional:
#    -NoDeploy       only build, do not deploy
#    -RetroArch PATH  RetroArch root dir (default E:\RetroArch\RetroArch-Win64)
#    -Msys64 PATH     MSYS2 root dir (default C:\msys64)
#
#  NOTE: keep this file pure ASCII. Windows PowerShell 5.1 reads .ps1 as
#  ANSI/GBK by default; non-ASCII chars in a UTF-8 (no BOM) file can break
#  the parser.
# ============================================================================
param(
    [switch]$NoDeploy,
    [string]$RetroArch = "E:\RetroArch\RetroArch-Win64",
    [string]$Msys64 = "C:\msys64"
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$bash = Join-Path $Msys64 "usr\bin\bash.exe"
$buildOut = Join-Path $projectRoot "src\build_libretro\main_libretro\towns_libretro.dll"
$infoSrc  = Join-Path $projectRoot "src\main_libretro\towns_libretro.info"

if (-not (Test-Path $bash)) {
    Write-Host "[ERROR] MSYS2 bash not found: $bash" -ForegroundColor Red
    exit 1
}

Write-Host "==> Building towns_libretro (MSYS2 MinGW) ..." -ForegroundColor Cyan
# Export PATH in the same command (MSYS2 bash default PATH lacks mingw64),
# otherwise cc1plus cannot find its runtime DLLs.
$msysSrc = ($projectRoot -replace '\\', '/')
$msysSrc = $msysSrc -replace '^([A-Za-z]):', '/$1'
& $bash -lc ("export PATH=" + $Msys64 + "/mingw64/bin:/usr/bin:`$PATH; cd " + $msysSrc + "/src/main_libretro && sh build_libretro.sh")
if ($LASTEXITCODE -ne 0) {
    Write-Host "[ERROR] Build failed, exit code $LASTEXITCODE" -ForegroundColor Red
    exit 1
}

if (-not (Test-Path $buildOut)) {
    Write-Host "[ERROR] Build output not found: $buildOut" -ForegroundColor Red
    exit 1
}

if ($NoDeploy) {
    Write-Host "==> Build done (not deployed): $buildOut" -ForegroundColor Green
    exit 0
}

# ---------- deploy ----------
$cores = Join-Path $RetroArch "cores"
$info  = Join-Path $RetroArch "info"
$targetDll = Join-Path $cores "towns_libretro.dll"
$targetInfo = Join-Path $info "towns_libretro.info"

if (-not (Test-Path $RetroArch)) {
    Write-Host "[ERROR] RetroArch not found: $RetroArch" -ForegroundColor Red
    exit 1
}

# Ensure RetroArch is fully closed (it locks cores/*.dll, making cp fail silently)
$ra = Get-Process retroarch -ErrorAction SilentlyContinue
if ($null -ne $ra) {
    Write-Host "[ERROR] RetroArch is running. Close it completely first, otherwise the DLL is locked." -ForegroundColor Red
    exit 1
}

New-Item -ItemType Directory -Force -Path $cores | Out-Null
New-Item -ItemType Directory -Force -Path $info  | Out-Null

# Backup old DLL
if (Test-Path $targetDll) {
    Copy-Item $targetDll "$targetDll.bak" -Force
    Write-Host "    Old DLL backed up as towns_libretro.dll.bak"
}

Copy-Item $buildOut $targetDll -Force
Copy-Item $infoSrc $targetInfo -Force

# Hard verify: deployed DLL must be byte-identical to the build output
$a = [System.IO.File]::ReadAllBytes($buildOut)
$b = [System.IO.File]::ReadAllBytes($targetDll)
$same = ($a.Length -eq $b.Length)
if ($same) {
    for ($i = 0; $i -lt $a.Length; $i++) { if ($a[$i] -ne $b[$i]) { $same = $false; break } }
}
if (-not $same) {
    Write-Host "[ERROR] Deploy verify failed: cores\ DLL differs from build output!" -ForegroundColor Red
    exit 1
}

Write-Host "==> Deployed OK (byte-identical):" -ForegroundColor Green
Write-Host "    DLL : $targetDll"
Write-Host "    size: $($a.Length) bytes"
Write-Host "    INFO: $targetInfo"
Write-Host "==> Ensure system\ has FMT_SYS.ROM, then start RetroArch and Load Content." -ForegroundColor Cyan
