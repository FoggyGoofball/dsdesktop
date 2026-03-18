# Wii Homebrew Deployment Script (PowerShell)
# 
# Usage: .\deploy_to_sd.ps1 -SDCardPath "E:\" -BuildConfig "Release"
#
# Deploys wii_proxy and all supporting files to an SD card for Homebrew Channel

param(
    [string]$SDCardPath = "E:\",
    [string]$BuildConfig = "Release",
    [switch]$GenerateIcons = $false
)

$ErrorActionPreference = "Stop"

# Validate SD card path
if (-not (Test-Path $SDCardPath)) {
    Write-Error "SD card path not found: $SDCardPath"
    exit 1
}

$dsremoteDir = Join-Path $SDCardPath "apps\dsremote"

# Create directory structure
Write-Host "Creating directory structure at: $dsremoteDir" -ForegroundColor Green
New-Item -ItemType Directory -Path $dsremoteDir -Force | Out-Null

# Verify required files exist
$requiredFiles = @(
    @{ Source = "wii_proxy\wii_proxy.dol"; Dest = "$dsremoteDir\boot.dol"; Name = "boot.dol" }
    @{ Source = "wii_proxy\meta.xml"; Dest = "$dsremoteDir\meta.xml"; Name = "meta.xml" }
    @{ Source = "wii_proxy\proxy.cfg"; Dest = "$dsremoteDir\proxy.cfg"; Name = "proxy.cfg" }
)

# Copy required files
foreach ($file in $requiredFiles) {
    if (-not (Test-Path $file.Source)) {
        Write-Error "Required file not found: $($file.Source)"
        exit 1
    }
    Write-Host "  Copying $($file.Name)..." -ForegroundColor Cyan
    Copy-Item -Path $file.Source -Destination $file.Dest -Force
}

# Optionally copy optional files
$optionalFiles = @(
    @{ Source = "wii_proxy\icon.png"; Dest = "$dsremoteDir\icon.png"; Name = "icon.png" }
    @{ Source = "wii_proxy\banner.png"; Dest = "$dsremoteDir\banner.png"; Name = "banner.png" }
)

foreach ($file in $optionalFiles) {
    if (Test-Path $file.Source) {
        Write-Host "  Copying $($file.Name)..." -ForegroundColor Cyan
        Copy-Item -Path $file.Source -Destination $file.Dest -Force
    }
}

# Generate icons if requested and Python/PIL available
if ($GenerateIcons) {
    Write-Host "Generating icons with Python..." -ForegroundColor Yellow
    $pythonScript = "wii_proxy\generate_icons.py"
    
    if (Test-Path $pythonScript) {
        try {
            $output = & python $pythonScript 2>&1
            Write-Host $output -ForegroundColor Gray
            
            if (Test-Path "icon.png") {
                Copy-Item "icon.png" "$dsremoteDir\icon.png" -Force
            }
            if (Test-Path "banner.png") {
                Copy-Item "banner.png" "$dsremoteDir\banner.png" -Force
            }
        } catch {
            Write-Host "  Note: Python icon generation skipped (PIL not available)" -ForegroundColor Yellow
        }
    }
}

# Verify deployment
Write-Host ""
Write-Host "✓ Deployment complete!" -ForegroundColor Green
Write-Host ""
Write-Host "SD card layout:" -ForegroundColor Cyan
Get-ChildItem $dsremoteDir | Format-Table Name, Length

Write-Host ""
Write-Host "Next steps:" -ForegroundColor Green
Write-Host "1. Safely eject SD card from PC"
Write-Host "2. Insert SD card into Wii"
Write-Host "3. Go to Wii Menu → Homebrew Channel"
Write-Host "4. Select 'DS Remote Desktop' and press A"
Write-Host ""
Write-Host "Before launching, ensure:" -ForegroundColor Yellow
Write-Host "  • DS is ready with haxxstation.nds (if using Download Play)"
Write-Host "  • PC host is running: .\pc_host\build\Release\dsrd_host.exe --wii <WII_IP>"
Write-Host "  • Wii is connected to network (USB Ethernet or Wi-Fi)"
