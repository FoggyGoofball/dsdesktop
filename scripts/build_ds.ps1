# scripts/build_ds.ps1 - DS client build using Wonderful/BlocksDS toolchain
param([switch]$Clean)
$ErrorActionPreference = "Stop"
$Workspace = $PSScriptRoot | Split-Path
$BuildRoot = "C:\sourcecode\dsdesktop"
$BashExe   = "C:\devkitpro\msys2\usr\bin\bash.exe"
if (!(Test-Path $BashExe)) { Write-Error "msys2 bash not found"; exit 1 }
foreach ($d in @("ds_client","common")) {
    robocopy (Join-Path $Workspace $d) (Join-Path $BuildRoot $d) /MIR /NFL /NDL /NJH /NJS /NP /XD build | Out-Null
}
Write-Host "[sync] ds_client, common -> $BuildRoot" -ForegroundColor Cyan
$cmd = "export PATH=/opt/wonderful/toolchain/gcc-arm-none-eabi/bin:/opt/wonderful/bin:/opt/devkitpro/tools/bin:`$PATH; export BLOCKSDS=/opt/wonderful/thirdparty/blocksds/core; cd /c/sourcecode/dsdesktop/ds_client;"
if ($Clean) { $cmd += " make clean 2>&1;" }
$cmd += " make 2>&1"
Write-Host "[build] Running DS client make..." -ForegroundColor Yellow
& $BashExe -c $cmd 2>&1 | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) { Write-Error "Build FAILED"; exit 1 }
$nds = "$BuildRoot\ds_client\dsremote.nds"
if (Test-Path $nds) {
    Copy-Item $nds "$Workspace\ds_client\dsremote.nds" -Force
    Copy-Item $nds "$Workspace\dsremote.nds" -Force
    Write-Host "[done] dsremote.nds ($([math]::Round((Get-Item $nds).Length/1KB,1)) KB)" -ForegroundColor Green
} else { Write-Error "dsremote.nds not found"; exit 1 }
