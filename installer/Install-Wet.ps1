# wet-recomp installer
#
# Builds the native Windows port of Wet from the user's own Xbox 360 disc image.
# Nothing from the game is downloaded or shipped: the ISO is extracted and recompiled on this
# machine. Layout, next to this script:
#   source\   the wet-recomp source (shipped in the release zip)
#   game\     files extracted from the user's ISO
#   Wet\      the finished port (wet.exe, its DLLs, wet.toml with the player's settings)
#
# Usage: double-click Install-Wet.bat, or
#   powershell -ExecutionPolicy Bypass -File Install-Wet.ps1 [-IsoPath X] [-Yes] [-NoShortcut]

param(
    [string]$IsoPath = "",
    [switch]$Yes,          # do not ask before installing missing tools
    [switch]$NoShortcut,
    [switch]$SkipTools     # assume the build tools are present (testing)
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$Source = Join-Path $Root "source"
$GameDir = Join-Path $Root "game"
$WetDir = Join-Path $Root "Wet"
$LogFile = Join-Path $Root "install.log"
$ExpectedXexSize = 22364160

function Say([string]$text, [string]$color = "Gray") { Write-Host $text -ForegroundColor $color }
function Step([string]$text) { Write-Host ""; Write-Host "== $text" -ForegroundColor Cyan }
function Fail([string]$text) {
    Write-Host ""
    Write-Host "ERROR: $text" -ForegroundColor Red
    Write-Host "Details are in $LogFile" -ForegroundColor Red
    throw $text
}

function Refresh-Path {
    $machine = [Environment]::GetEnvironmentVariable("Path", "Machine")
    $user = [Environment]::GetEnvironmentVariable("Path", "User")
    $env:Path = "$machine;$user"
}

# Runs a program, appends its output to the log, shows only the last lines on failure.
function Run([string]$what, [string]$exe, [string[]]$arguments, [string]$cwd = $Root) {
    Say "   $what ..."
    Add-Content -Path $LogFile -Value "`r`n>>> $exe $($arguments -join ' ')"
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.Arguments = ($arguments | ForEach-Object { if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }) -join ' '
    $psi.WorkingDirectory = $cwd
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $psi.CreateNoWindow = $true
    $proc = New-Object System.Diagnostics.Process
    $proc.StartInfo = $psi
    $lines = New-Object System.Collections.Generic.List[string]
    $handler = { if ($EventArgs.Data -ne $null) { $Event.MessageData.Add($EventArgs.Data) } }
    $o = Register-ObjectEvent -InputObject $proc -EventName OutputDataReceived -Action $handler -MessageData $lines
    $e = Register-ObjectEvent -InputObject $proc -EventName ErrorDataReceived -Action $handler -MessageData $lines
    [void]$proc.Start()
    $proc.BeginOutputReadLine()
    $proc.BeginErrorReadLine()
    $shown = 0
    while (-not $proc.HasExited) {
        Start-Sleep -Milliseconds 500
        $n = $lines.Count
        if ($n -gt $shown) {
            $last = $lines[$n - 1]
            if ($last.Length -gt 100) { $last = $last.Substring(0, 100) }
            Write-Host ("`r      " + $last.PadRight(100)) -NoNewline -ForegroundColor DarkGray
            $shown = $n
        }
    }
    $proc.WaitForExit()
    Start-Sleep -Milliseconds 200
    Unregister-Event -SourceIdentifier $o.Name
    Unregister-Event -SourceIdentifier $e.Name
    Write-Host ("`r" + "".PadRight(108) + "`r") -NoNewline
    Add-Content -Path $LogFile -Value ($lines -join "`r`n")
    if ($proc.ExitCode -ne 0) {
        $lines | Select-Object -Last 15 | ForEach-Object { Say "      $_" "DarkGray" }
        Fail "$what failed (exit code $($proc.ExitCode))."
    }
}

function Find-Python {
    foreach ($candidate in @(@("py", "-3"), @("python"))) {
        $cmd = Get-Command $candidate[0] -ErrorAction SilentlyContinue
        if ($cmd) {
            $args2 = @()
            if ($candidate.Count -gt 1) { $args2 = $candidate[1..($candidate.Count - 1)] }
            try {
                $v = & $cmd.Source @args2 -c "import sys; print('%d.%d' % sys.version_info[:2])" 2>$null
                if ($LASTEXITCODE -eq 0 -and $v -and ([version]$v -ge [version]"3.10")) {
                    return @{ Exe = $cmd.Source; Args = $args2 }
                }
            } catch {}
        }
    }
    return $null
}

function Find-VsCpp {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) { return $false }
    $path = & $vswhere -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null
    return [bool]$path
}

function Llvm-Bin {
    if ($env:LLVM_BIN -and (Test-Path (Join-Path $env:LLVM_BIN "clang.exe"))) { return $env:LLVM_BIN }
    $default = Join-Path $env:ProgramFiles "LLVM\bin"
    if (Test-Path (Join-Path $default "clang.exe")) { return $default }
    return $null
}

# ---------------------------------------------------------------------------------------------
Clear-Host
Say "Wet native PC port - installer" "White"
Say "This builds the game from YOUR OWN Xbox 360 disc image. Nothing from the game is"
Say "downloaded; your ISO is extracted and recompiled on this computer."
Set-Content -Path $LogFile -Value "wet-recomp install log $(Get-Date -Format s)"

if (-not (Test-Path (Join-Path $Source "scripts\build.py"))) {
    Fail "The 'source' folder is missing. Extract the whole release zip, then run Install-Wet.bat again."
}
if ($Root.Length -gt 60) {
    Say ""
    Say "Note: this folder has a long path ($($Root.Length) characters). If the build fails with" "Yellow"
    Say "path errors, move the folder somewhere short like C:\Games\Wet and run it again." "Yellow"
}
if ($Root -match ' ') {
    Say ""
    Say "Note: this folder's path contains spaces. That should work, but a path without spaces" "Yellow"
    Say "(for example C:\Games\Wet) is the most tested." "Yellow"
}

# ---- 1. The disc image ----------------------------------------------------------------------
Step "1/5  Your Wet disc image"
$haveGame = Test-Path (Join-Path $GameDir "default.xex")
if ($haveGame) {
    Say "   Game files already extracted in $GameDir - skipping."
} else {
    if (-not $IsoPath) {
        Add-Type -AssemblyName System.Windows.Forms
        $dialog = New-Object System.Windows.Forms.OpenFileDialog
        $dialog.Title = "Select your Wet (Xbox 360) disc image"
        $dialog.Filter = "Xbox 360 disc image (*.iso)|*.iso|All files (*.*)|*.*"
        if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) { Fail "No disc image selected." }
        $IsoPath = $dialog.FileName
    }
    if (-not (Test-Path $IsoPath)) { Fail "Disc image not found: $IsoPath" }
    Say "   $IsoPath"
}

# ---- 2. Build tools -------------------------------------------------------------------------
Step "2/5  Build tools"
if (-not $SkipTools) {
    $missing = @()
    if (-not (Get-Command git -ErrorAction SilentlyContinue)) { $missing += @{ Name = "Git"; Id = "Git.Git"; Extra = @() } }
    if (-not (Find-Python)) { $missing += @{ Name = "Python 3"; Id = "Python.Python.3.12"; Extra = @() } }
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { $missing += @{ Name = "CMake"; Id = "Kitware.CMake"; Extra = @() } }
    if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) { $missing += @{ Name = "Ninja"; Id = "Ninja-build.Ninja"; Extra = @() } }
    if (-not (Llvm-Bin)) { $missing += @{ Name = "LLVM / Clang"; Id = "LLVM.LLVM"; Extra = @() } }
    if (-not (Find-VsCpp)) {
        $missing += @{ Name = "Visual Studio 2022 Build Tools (C++)"; Id = "Microsoft.VisualStudio.2022.BuildTools";
                       Extra = @("--override", "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended") }
    }
    if ($missing.Count -eq 0) {
        Say "   Everything needed is already installed."
    } else {
        Say "   These free tools are needed to build the game and are not installed yet:"
        $missing | ForEach-Object { Say "     - $($_.Name)" "White" }
        Say "   They are installed with winget from their official sources (about 8 GB if the"
        Say "   Visual Studio Build Tools are included). Windows may ask for administrator approval."
        if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
            Fail "winget is not available. Install 'App Installer' from the Microsoft Store, or install the tools above yourself."
        }
        if (-not $Yes) {
            $answer = Read-Host "   Install them now? [Y/n]"
            if ($answer -and $answer -notmatch '^[Yy]') { Fail "Cancelled. Install the tools above, then run the installer again." }
        }
        foreach ($tool in $missing) {
            $wingetArgs = @("install", "--id", $tool.Id, "-e", "--source", "winget",
                            "--accept-source-agreements", "--accept-package-agreements", "--silent") + $tool.Extra
            Run "Installing $($tool.Name) (this can take a while)" "winget" $wingetArgs
        }
        Refresh-Path
        if (-not (Find-VsCpp)) { Fail "The Visual Studio C++ build tools are still missing after the install. A reboot may be needed; then run the installer again." }
        if (-not (Llvm-Bin)) { Fail "Clang was not found after installing LLVM. Set LLVM_BIN to LLVM's bin folder and run again." }
    }
}
$python = Find-Python
if (-not $python) { Fail "Python 3.10 or newer was not found." }
$llvm = Llvm-Bin
if ($llvm) { $env:LLVM_BIN = $llvm }

# ---- 3. Extract the disc --------------------------------------------------------------------
Step "3/5  Extracting game files"
if (-not $haveGame) {
    Run "Extracting the disc image (about 7 GB)" $python.Exe ($python.Args + @((Join-Path $Source "tools\xdvdfs_extract.py"), "extract", $IsoPath, $GameDir))
}
$xex = Get-Item (Join-Path $GameDir "default.xex") -ErrorAction SilentlyContinue
if (-not $xex) { Fail "default.xex was not found in the disc image. Is this a Wet (Xbox 360) disc?" }
if ($xex.Length -ne $ExpectedXexSize) {
    Say "   Warning: default.xex is $($xex.Length) bytes; the tested version is $ExpectedXexSize." "Yellow"
    Say "   A different region or revision may not build or run correctly." "Yellow"
} else {
    Say "   default.xex matches the tested version."
}

# ---- 4. Build -------------------------------------------------------------------------------
Step "4/5  Building (recompiling the game for PC)"
Say "   The first build downloads the ReXGlue SDK and takes 10-30 minutes depending on your CPU."
$build = Join-Path $Source "scripts\build.py"
Run "Building the SDK" $python.Exe ($python.Args + @($build, "sdk")) $Source
Run "Recompiling and building Wet" $python.Exe ($python.Args + @($build, "game")) $Source

# ---- 5. Install -----------------------------------------------------------------------------
Step "5/5  Installing"
$out = Join-Path $Source "out\build\win-amd64-relwithdebinfo"
if (-not (Test-Path (Join-Path $out "wet.exe"))) { Fail "The build finished but wet.exe was not produced." }
New-Item -ItemType Directory -Force -Path $WetDir | Out-Null
Get-ChildItem $out -File | Where-Object {
    ($_.Name -eq "wet.exe") -or ($_.Name -eq "wet.pdb") -or ($_.Extension -eq ".dll") -or
    ($_.Extension -eq ".pdb" -and $_.BaseName -like "rex*")
} | ForEach-Object { Copy-Item $_.FullName -Destination $WetDir -Force }
Say "   Installed to $WetDir"
Say "   (Your settings in Wet\wet.toml are kept when you run the installer again.)"

if (-not $NoShortcut) {
    $desktop = [Environment]::GetFolderPath("Desktop")
    $shell = New-Object -ComObject WScript.Shell
    $link = $shell.CreateShortcut((Join-Path $desktop "Wet.lnk"))
    $link.TargetPath = Join-Path $WetDir "wet.exe"
    $link.WorkingDirectory = $WetDir
    $link.Description = "Wet (native PC port)"
    $link.Save()
    Say "   Desktop shortcut created."
}

Say ""
Say "Done. Start the game with the 'Wet' shortcut on your desktop, or Wet\wet.exe." "Green"
Say "Press F1 in game for controls and display settings." "Green"
