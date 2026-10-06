# Prepares the game files for the Ridge Racer 6 PC test build.
# Finds the single .iso in the PUT-ISO-HERE folder, extracts the Xbox 360 game
# partition (XDVDFS) into the "game" folder, and checks that the executable is
# the exact version this build was made from.
# Works with the PowerShell that ships with Windows 10/11; needs nothing else.
param(
    [string]$IsoDir = 'PUT-ISO-HERE',
    [string]$OutDir = 'game',
    [string]$ExpectedSha256 = '39D3C0004EC62AEB0FE3E7E1889CF25D98FBD27987B6BC6B5FF30A56FFBA6C00',
    [switch]$ListOnly,
    [switch]$NoSpaceCheck
)
$ErrorActionPreference = 'Stop'
$SectorSize = 2048   # PowerShell names are case-insensitive: keep this distinct from $sector below
$Marker = '.rr6-ready'

function Fail([int]$code, [string]$message) {
    Write-Host ''
    Write-Host $message -ForegroundColor Red
    exit $code
}

function Test-GameVersion {
    $xex = Join-Path $OutDir 'default.xex'
    if (-not (Test-Path -LiteralPath $xex)) { return $false }
    $got = (Get-FileHash -Algorithm SHA256 -LiteralPath $xex).Hash
    if ($got -ne $ExpectedSha256) {
        Fail 4 ("This is a different version of the game than this build was made for.`n" +
                "This build only works with the USA disc (title ID 4E4D07D3).`n" +
                "Expected default.xex SHA-256: $ExpectedSha256`n" +
                "Found:                        $got")
    }
    return $true
}

# Already extracted (or files supplied by hand)? Then only the version check is needed.
if (-not $ListOnly -and (Test-Path -LiteralPath (Join-Path $OutDir 'default.xex'))) {
    if (Test-Path -LiteralPath (Join-Path $OutDir $Marker)) { exit 0 }
}

# ---- locate the ISO -------------------------------------------------------
if (-not (Test-Path -LiteralPath $IsoDir)) { Fail 2 "Folder '$IsoDir' is missing. Re-extract the test build zip." }
$isos = @(Get-ChildItem -LiteralPath $IsoDir -File | Where-Object { $_.Extension -ieq '.iso' })
if ($isos.Count -eq 0) {
    # Files without the marker are accepted as supplied by hand, unless the launcher
    # started copying them out of a disc image and did not finish ('.rr6-copying').
    $unfinished = Test-Path -LiteralPath (Join-Path $OutDir '.rr6-copying')
    if (-not $unfinished -and (Test-GameVersion)) { Set-Content -LiteralPath (Join-Path $OutDir $Marker) -Value 'ok'; exit 0 }
    Fail 2 ("No .iso file found.`n" +
            "Put your Ridge Racer 6 (USA) Xbox 360 disc image into the '$IsoDir' folder`n" +
            "and start this launcher again.")
}
if ($isos.Count -gt 1) {
    Fail 3 "There are $($isos.Count) .iso files in '$IsoDir'. Leave only the Ridge Racer 6 one there."
}
$iso = $isos[0].FullName
Write-Host "Disc image: $($isos[0].Name)"

$fs = [System.IO.File]::Open($iso, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read,
                             [System.IO.FileShare]::Read)
function Read-At([long]$position, [int]$count) {
    [void]$fs.Seek($position, [System.IO.SeekOrigin]::Begin)
    $buffer = New-Object byte[] $count
    $done = 0
    while ($done -lt $count) {
        $n = $fs.Read($buffer, $done, $count - $done)
        if ($n -le 0) { throw 'Unexpected end of the disc image (file is truncated?).' }
        $done += $n
    }
    return ,$buffer
}

# ---- find the game partition ----------------------------------------------
# Offsets of the XDVDFS partition for the known Xbox 360 disc layouts, then 0
# for an image that contains only the game partition.
$magic = [System.Text.Encoding]::ASCII.GetBytes('MICROSOFT*XBOX*MEDIA')
$base = [long]-1
$rootSector = [uint32]0
$rootSize = [uint32]0
foreach ($candidate in @([long]0xFD90000, [long]0x2080000, [long]0x18300000, [long]0)) {
    if (($candidate + 0x10000 + 28) -gt $fs.Length) { continue }
    $header = Read-At ($candidate + 0x10000) 28
    $match = $true
    for ($i = 0; $i -lt $magic.Length; $i++) { if ($header[$i] -ne $magic[$i]) { $match = $false; break } }
    if ($match) {
        $base = $candidate
        $rootSector = [BitConverter]::ToUInt32($header, 20)
        $rootSize = [BitConverter]::ToUInt32($header, 24)
        break
    }
}
if ($base -lt 0) { $fs.Close(); Fail 5 'This file is not an Xbox 360 game disc image (no game partition found).' }

# ---- read the directory tree ----------------------------------------------
$latin1 = [System.Text.Encoding]::GetEncoding(28591)
$sep = [System.IO.Path]::DirectorySeparatorChar
$files = New-Object System.Collections.Generic.List[object]
$folders = New-Object System.Collections.Generic.List[string]
$pending = New-Object System.Collections.Generic.Stack[object]
$pending.Push(@{ Sector = $rootSector; Size = $rootSize; Path = '' })
while ($pending.Count -gt 0) {
    $dir = $pending.Pop()
    if ($dir.Size -eq 0) { continue }
    $table = Read-At ($base + [long]$dir.Sector * $SectorSize) ([int]$dir.Size)
    $nodes = New-Object System.Collections.Generic.Stack[int]
    $nodes.Push(0)
    $seen = @{}
    while ($nodes.Count -gt 0) {
        $o = $nodes.Pop()
        if ($seen.ContainsKey($o) -or ($o + 14) -gt $table.Length) { continue }
        $seen[$o] = $true
        $left = [BitConverter]::ToUInt16($table, $o)
        $right = [BitConverter]::ToUInt16($table, $o + 2)
        $sector = [BitConverter]::ToUInt32($table, $o + 4)
        $size = [BitConverter]::ToUInt32($table, $o + 8)
        $attributes = $table[$o + 12]
        $nameLength = $table[$o + 13]
        if ($left -eq 0xFFFF -and $right -eq 0xFFFF -and $sector -eq [uint32]::MaxValue) { continue }
        $name = $latin1.GetString($table, $o + 14, $nameLength)
        if ($name -eq '' -or $name -eq '.' -or $name -eq '..' -or $name.IndexOfAny([char[]]'\/:*?"<>|') -ge 0) {
            $fs.Close(); Fail 6 "The disc image contains an unsafe file name; refusing to extract."
        }
        if ($dir.Path -eq '') { $path = $name } else { $path = $dir.Path + $sep + $name }
        if (($attributes -band 0x10) -ne 0) {
            $folders.Add($path)
            $pending.Push(@{ Sector = $sector; Size = $size; Path = $path })
        } else {
            $files.Add(@{ Path = $path; Sector = $sector; Size = $size })
        }
        if ($left -ne 0 -and $left -ne 0xFFFF) { $nodes.Push([int]$left * 4) }
        if ($right -ne 0 -and $right -ne 0xFFFF) { $nodes.Push([int]$right * 4) }
    }
}
$totalBytes = [long]0
foreach ($f in $files) { $totalBytes += [long]$f.Size }

if ($ListOnly) {
    foreach ($f in ($files | Sort-Object { $_.Path.ToLowerInvariant() })) { "f {0,12} {1}" -f $f.Size, $f.Path }
    "# {0} files, {1} bytes, {2} dirs" -f $files.Count, $totalBytes, $folders.Count
    $fs.Close()
    exit 0
}

if (-not ($files | Where-Object { $_.Path -ieq 'default.xex' })) {
    $fs.Close(); Fail 5 'This disc image has no default.xex, so it is not a game disc this build can use.'
}

# ---- extract ----------------------------------------------------------------
[void](New-Item -ItemType Directory -Force -Path $OutDir)
try {
    $drive = New-Object System.IO.DriveInfo ([System.IO.Path]::GetPathRoot((Resolve-Path -LiteralPath $OutDir).Path))
    if (-not $NoSpaceCheck -and $drive.AvailableFreeSpace -lt ($totalBytes + 200MB)) {
        $fs.Close()
        Fail 7 ("Not enough free disk space. The game files need {0:N1} GB; drive {1} has {2:N1} GB free." -f
                ($totalBytes / 1GB), $drive.Name, ($drive.AvailableFreeSpace / 1GB))
    }
} catch [System.ArgumentException] { }   # free-space check is best effort

foreach ($folder in $folders) { [void](New-Item -ItemType Directory -Force -Path (Join-Path $OutDir $folder)) }
Write-Host ("Extracting {0} files, {1:N1} GB. This happens once and takes a few minutes." -f $files.Count, ($totalBytes / 1GB))
$chunk = New-Object byte[] (4MB)
$doneBytes = [long]0
$index = 0
foreach ($f in ($files | Sort-Object { $_.Sector })) {
    $index++
    $target = Join-Path $OutDir $f.Path
    $size = [long]$f.Size
    if ((Test-Path -LiteralPath $target) -and ((Get-Item -LiteralPath $target).Length -eq $size)) {
        $doneBytes += $size
        continue
    }
    Write-Host ("  [{0,2}/{1}] {2,3:N0}%  {3}" -f $index, $files.Count, (100 * $doneBytes / [Math]::Max([long]1, $totalBytes)), $f.Path)
    $partial = $target + '.part'
    [void]$fs.Seek($base + [long]$f.Sector * $SectorSize, [System.IO.SeekOrigin]::Begin)
    $out = [System.IO.File]::Open($partial, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write,
                                  [System.IO.FileShare]::None)
    try {
        $remaining = $size
        while ($remaining -gt 0) {
            $want = [int][Math]::Min([long]$chunk.Length, $remaining)
            $n = $fs.Read($chunk, 0, $want)
            if ($n -le 0) { throw "Unexpected end of the disc image while reading $($f.Path)." }
            $out.Write($chunk, 0, $n)
            $remaining -= $n
        }
    } finally {
        $out.Close()
    }
    Move-Item -LiteralPath $partial -Destination $target -Force
    $doneBytes += $size
}
$fs.Close()

if (-not (Test-GameVersion)) { Fail 5 'Extraction finished but default.xex is missing.' }
Set-Content -LiteralPath (Join-Path $OutDir $Marker) -Value 'ok'
Remove-Item -LiteralPath (Join-Path $OutDir '.rr6-copying') -Force -ErrorAction SilentlyContinue
Write-Host 'Game files are ready.' -ForegroundColor Green
exit 0
