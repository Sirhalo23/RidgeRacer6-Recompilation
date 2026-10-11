# Assembles a tester package (a zip with NO game data) from the current Windows
# build. Run it through make-tester-package.bat in the project folder.
#
#   dist\RidgeRacer6-PC-TestBuild-NN.zip   give this to testers
#   dist\RidgeRacer6-PC-TestBuild-NN.map   keep private (crash address lookup)
#
# NN is one more than the highest number already in dist\, or -Number.
param(
    [string]$ProjectDir = (Split-Path -Parent $PSScriptRoot),
    [string]$BuildDir = '',
    [string]$DistDir = '',
    [int]$Number = 0,
    [switch]$KeepFolder
)
$ErrorActionPreference = 'Stop'

function Fail([string]$message) {
    Write-Host ''
    Write-Host ('ERROR: ' + $message) -ForegroundColor Red
    exit 1
}

$ProjectDir = (Resolve-Path -LiteralPath $ProjectDir).Path
if (-not $BuildDir) { $BuildDir = Join-Path (Join-Path (Join-Path $ProjectDir 'out') 'build') 'win-amd64-release' }
if (-not $DistDir) { $DistDir = Join-Path (Split-Path -Parent $ProjectDir) 'dist' }
$packageSrc = Join-Path $ProjectDir 'package'

# ---- what goes in -----------------------------------------------------------
$exe = Join-Path $BuildDir 'rr6_recomp.exe'
$needed = @(
    $exe,
    (Join-Path $BuildDir 'rexruntime.dll'),
    (Join-Path $BuildDir 'rexgpu-xenos.dll'),
    (Join-Path $ProjectDir 'RR6 Launcher.exe'),
    (Join-Path $ProjectDir 'gamecontrollerdb.txt'),
    (Join-Path (Join-Path $ProjectDir 'assets') 'achievement.wav'),
    (Join-Path (Join-Path $ProjectDir 'config') 'rr6_recomp.default.toml'),
    (Join-Path $packageSrc 'README.txt'),
    (Join-Path $packageSrc 'Play without the launcher.bat'),
    (Join-Path $packageSrc 'Play without the launcher (diagnostic).bat'),
    (Join-Path (Join-Path $packageSrc 'tools') 'prepare-game.ps1'),
    (Join-Path (Join-Path $packageSrc 'tools') 'collect-report.ps1'),
    (Join-Path (Join-Path $packageSrc 'PUT-ISO-HERE') 'ONLY NEEDED FOR Play without the launcher.bat.txt'),
    (Join-Path (Join-Path $packageSrc 'DLC') 'PUT-CONTENT-FILES-HERE.txt')
)
foreach ($f in $needed) {
    if (-not (Test-Path -LiteralPath $f)) { Fail ("Missing: $f`nBuild the game first (build-windows.bat).") }
}

# ---- package number ---------------------------------------------------------
[void](New-Item -ItemType Directory -Force -Path $DistDir)
if ($Number -le 0) {
    $highest = 0
    foreach ($z in @(Get-ChildItem -LiteralPath $DistDir -File | Where-Object { $_.Name -match '^RidgeRacer6-PC-TestBuild-(\d+)\.zip$' })) {
        [void]($z.Name -match '^RidgeRacer6-PC-TestBuild-(\d+)\.zip$')
        $n = [int]$Matches[1]
        if ($n -gt $highest) { $highest = $n }
    }
    $Number = $highest + 1
}
$tag = '{0:D2}' -f $Number
$name = "RidgeRacer6-PC-TestBuild-$tag"
$stage = Join-Path $DistDir $name
$zip = Join-Path $DistDir "$name.zip"
if (Test-Path -LiteralPath $zip) { Fail "$zip already exists. Use -Number to pick another number." }
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }

Write-Host "Assembling $name ..."
foreach ($d in @('', 'bin', (Join-Path 'bin' 'sounds'), 'tools', 'licenses', 'PUT-ISO-HERE', 'DLC')) {
    [void](New-Item -ItemType Directory -Force -Path (Join-Path $stage $d))
}

# Text files get Windows line endings so they read properly in Notepad and so
# cmd.exe handles the batch files' labels.
function Copy-Text([string]$from, [string]$to, [hashtable]$replace = @{}) {
    $text = [System.IO.File]::ReadAllText($from)
    foreach ($k in $replace.Keys) { $text = $text.Replace($k, $replace[$k]) }
    $text = $text.Replace("`r`n", "`n").Replace("`n", "`r`n")
    [System.IO.File]::WriteAllText($to, $text, (New-Object System.Text.UTF8Encoding($false)))
}

Copy-Item -LiteralPath (Join-Path $ProjectDir 'RR6 Launcher.exe') -Destination $stage
Copy-Item -LiteralPath (Join-Path $ProjectDir 'gamecontrollerdb.txt') -Destination $stage
Copy-Text (Join-Path $packageSrc 'README.txt') (Join-Path $stage 'README.txt') @{ '@BUILD@' = $tag }
Copy-Text (Join-Path $packageSrc 'Play without the launcher.bat') (Join-Path $stage 'Play without the launcher.bat')
Copy-Text (Join-Path $packageSrc 'Play without the launcher (diagnostic).bat') (Join-Path $stage 'Play without the launcher (diagnostic).bat')
Copy-Text (Join-Path (Join-Path $packageSrc 'PUT-ISO-HERE') 'ONLY NEEDED FOR Play without the launcher.bat.txt') `
          (Join-Path (Join-Path $stage 'PUT-ISO-HERE') 'ONLY NEEDED FOR Play without the launcher.bat.txt')
# Empty but for its note: players put their own content files there.
Copy-Text (Join-Path (Join-Path $packageSrc 'DLC') 'PUT-CONTENT-FILES-HERE.txt') `
          (Join-Path (Join-Path $stage 'DLC') 'PUT-CONTENT-FILES-HERE.txt')
Copy-Item -LiteralPath (Join-Path (Join-Path $packageSrc 'tools') 'prepare-game.ps1') -Destination (Join-Path $stage 'tools')
Copy-Item -LiteralPath (Join-Path (Join-Path $packageSrc 'tools') 'collect-report.ps1') -Destination (Join-Path $stage 'tools')
foreach ($lic in @(Get-ChildItem -LiteralPath (Join-Path $packageSrc 'licenses') -File)) {
    Copy-Item -LiteralPath $lic.FullName -Destination (Join-Path $stage 'licenses')
}
foreach ($b in @('rr6_recomp.exe', 'rexruntime.dll', 'rexgpu-xenos.dll')) {
    Copy-Item -LiteralPath (Join-Path $BuildDir $b) -Destination (Join-Path $stage 'bin')
}
# Personalize staged executables only; the source build and public default stay intact.
if ($env:RR6_ICON_XEX) {
    $iconTool = $env:RR6_ICON_REXGLUE
    if (-not $iconTool) {
        $sdkRoot = if ($env:REXSDK) { $env:REXSDK } else { Join-Path (Split-Path -Parent $ProjectDir) 'sdk/win-amd64' }
        $iconTool = Join-Path $sdkRoot 'bin/rexglue.exe'
    }
    $python = if ($env:PYTHON) { $env:PYTHON } else { 'python' }
    & $python (Join-Path $ProjectDir 'tools/disc_icon.py') $env:RR6_ICON_XEX $iconTool (Join-Path $stage 'icons') `
        --embed (Join-Path $stage 'RR6 Launcher.exe') --embed (Join-Path $stage 'bin/rr6_recomp.exe')
    if ($LASTEXITCODE -ne 0) { Fail 'Local disc-icon generation or embedding failed.' }
}
# The sound played with an achievement pop-up (an original chime; assets\achievement.wav).
Copy-Item -LiteralPath (Join-Path (Join-Path $ProjectDir 'assets') 'achievement.wav') `
          -Destination (Join-Path (Join-Path (Join-Path $stage 'bin') 'sounds') 'achievement.wav')
# Starting settings: plain 16:9. The launcher rewrites them for the tester's screen.
Copy-Item -LiteralPath (Join-Path (Join-Path $ProjectDir 'config') 'rr6_recomp.default.toml') `
          -Destination (Join-Path (Join-Path $stage 'bin') 'rr6_recomp.toml')

$exeHash = (Get-FileHash -LiteralPath (Join-Path $stage 'bin/rr6_recomp.exe') -Algorithm SHA256).Hash.ToLower()
$launcherHash = (Get-FileHash -LiteralPath (Join-Path $stage 'RR6 Launcher.exe') -Algorithm SHA256).Hash.ToLower()
$built = (Get-Item -LiteralPath $exe).LastWriteTime.ToString('yyyy-MM-dd HH:mm')
# Which SDK the two runtime files come from: the version header of the SDK next
# to the project (where build-windows.bat takes it from). Builds of our fork
# are numbered x.y.z.100 and up.
$sdkVersion = '0.10.0'
$sdkHeader = Join-Path (Join-Path (Join-Path (Join-Path (Join-Path (Split-Path -Parent $ProjectDir) 'sdk') 'win-amd64') 'include') 'rex') 'version.h'
if (Test-Path -LiteralPath $sdkHeader) {
    $found = Select-String -LiteralPath $sdkHeader -Pattern '^#define REXGLUE_VERSION_STRING "([^"]+)"' | Select-Object -First 1
    if ($found) { $sdkVersion = $found.Matches[0].Groups[1].Value }
}
$sdkSource = 'github.com/rexglue/rexglue-sdk'
if ($sdkVersion -match '^\d+\.\d+\.\d+\.(\d+)$' -and [int]$Matches[1] -ge 100) { $sdkSource = 'our fork, github.com/Sirhalo23/rexglue-sdk' }
$runtimeHash = (Get-FileHash -LiteralPath (Join-Path $BuildDir 'rexruntime.dll') -Algorithm SHA256).Hash.ToLower()
$info = @(
    "Ridge Racer 6 - PC test build $tag",
    "Built:        $built (Windows x64, Direct3D 12)",
    "Executable:   bin\rr6_recomp.exe  SHA-256 $exeHash",
    "Launcher:     RR6 Launcher.exe    SHA-256 $launcherHash",
    'Game version: USA disc, title ID 4E4D07D3, default.xex SHA-256',
    '              39D3C0004EC62AEB0FE3E7E1889CF25D98FBD27987B6BC6B5FF30A56FFBA6C00',
    "Runtime:      ReXGlue SDK $sdkVersion ($sdkSource)",
    "              bin\rexruntime.dll SHA-256 $runtimeHash"
) -join "`r`n"
[System.IO.File]::WriteAllText((Join-Path $stage 'BUILD-INFO.txt'), $info + "`r`n", (New-Object System.Text.UTF8Encoding($false)))

# ---- safety check: no game data may end up in the package -------------------
$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File)
$total = 0
foreach ($f in $files) {
    $total += $f.Length
    if ($f.Extension -match '^\.(xex|dat|sfd|iso|bin|xbe|pak)$') { Fail "Game data must not be packaged: $($f.FullName)" }
    if ($f.Length -gt 60MB) { Fail "Unexpectedly large file (game data?): $($f.FullName)" }
    # Content packages have no extension; they start with LIVE, PIRS or "CON ".
    if ($f.Length -ge 4) {
        $head = New-Object byte[] 4
        $in = [System.IO.File]::OpenRead($f.FullName)
        try { [void]$in.Read($head, 0, 4) } finally { $in.Close() }
        $magic = [System.Text.Encoding]::ASCII.GetString($head)
        if ($magic -eq 'LIVE' -or $magic -eq 'PIRS' -or $magic -eq 'CON ') {
            Fail "A content package must not be packaged: $($f.FullName)"
        }
    }
}
if ($total -gt 150MB) { Fail "The package is unexpectedly large ($([math]::Round($total / 1MB)) MB)." }

# ---- zip, private map file, notes -------------------------------------------
Add-Type -AssemblyName System.IO.Compression.FileSystem
[System.IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [System.IO.Compression.CompressionLevel]::Optimal, $true)
$map = Join-Path $BuildDir 'rr6_recomp.map'
$mapNote = 'no linker map was found for this build'
if (Test-Path -LiteralPath $map) {
    Copy-Item -LiteralPath $map -Destination (Join-Path $DistDir "$name.map") -Force
    $mapNote = "KEEP PRIVATE, do not send. Linker map for this exact build: turns a tester's crash offset into a function name (python tools/map_lookup.py dist/$name.map <offset>)"
}
$note = @(
    "$name.zip   the package to give to testers (no game data inside)",
    "$name.map   $mapNote",
    ''
) -join "`r`n"
Add-Content -LiteralPath (Join-Path $DistDir 'README-dist.txt') -Value $note
if (-not $KeepFolder) { Remove-Item -LiteralPath $stage -Recurse -Force }

Write-Host ''
Write-Host "Done: $zip" -ForegroundColor Green
Write-Host ("      {0} files, {1:N1} MB unpacked, {2:N1} MB zipped" -f $files.Count, ($total / 1MB), ((Get-Item -LiteralPath $zip).Length / 1MB))
Write-Host "      game executable built $built"
Write-Host ''
Write-Host 'Before sending it to anyone: unzip it somewhere, start RR6 Launcher.exe, press'
Write-Host '"Choose disc image..." and pick your .iso, to check the package itself works.'
exit 0
