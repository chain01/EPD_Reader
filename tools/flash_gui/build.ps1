param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$Sftool = "$env:USERPROFILE/.sifli/tools/sftool/0.2.5/sftool.exe",
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Sftool -PathType Leaf)) { throw "sftool not found: $Sftool" }
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$buildPath = Join-Path $env:TEMP 'SiFliFlashTool-build-1.0.0'
& $Python -m PyInstaller --noconfirm --clean --noupx --onefile --windowed --name SiFliFlashTool --add-binary "$Sftool;." --distpath $outputPath --workpath "$buildPath/work" --specpath $buildPath "$PSScriptRoot/flash_gui.py"
if ($LASTEXITCODE -ne 0) { throw "PyInstaller failed: $LASTEXITCODE" }
Copy-Item -LiteralPath "$PSScriptRoot/README.md" -Destination "$outputPath/使用说明.md"
