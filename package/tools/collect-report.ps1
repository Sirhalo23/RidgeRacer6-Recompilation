# Ridge Racer 6 PC test build - gathers what is needed for a bug report into
# bug-report.zip (in the package folder). Nothing is sent anywhere.
#
# Collected: the game's log files, the exit code, the settings in use, Windows'
# crash record for the game (if any), and the PC's Windows version, CPU, memory,
# graphics card and driver version.
#
# The logs contain file paths, and paths under C:\Users contain the Windows user
# name. In the copies that go into the zip that name is replaced by <user>, so
# the report can be attached to a public issue. The files in the logs folder
# itself are left as they are.
param(
    [string]$Root = (Split-Path -Parent $PSScriptRoot),
    [int]$CrashLookbackMinutes = 15
)
$ErrorActionPreference = 'Stop'
$logs = Join-Path $Root 'logs'
[void](New-Item -ItemType Directory -Force -Path $logs)

# Windows' own crash record for the game, if it wrote one.
$crashFile = Join-Path $logs 'crash-event.txt'
try {
    $since = (Get-Date).AddMinutes(-$CrashLookbackMinutes)
    $events = @(Get-WinEvent -FilterHashtable @{ LogName = 'Application'; StartTime = $since } -ErrorAction Stop |
        Where-Object { $_.Message -match 'rr6_recomp' } | Select-Object -First 4)
    if ($events.Count -gt 0) {
        $events | Format-List TimeCreated, ProviderName, Id, Message | Out-File -Encoding utf8 $crashFile
    } else {
        'No Windows crash record for the game in the last ' + $CrashLookbackMinutes + ' minutes.' |
            Out-File -Encoding utf8 $crashFile
    }
} catch {
    'Could not read the Windows event log: ' + $_.Exception.Message | Out-File -Encoding utf8 $crashFile
}

# Basic PC details.
$sysFile = Join-Path $logs 'system-info.txt'
try {
    $o = Get-CimInstance Win32_OperatingSystem
    $c = Get-CimInstance Win32_Processor | Select-Object -First 1
    $lines = @(
        'OS:  ' + $o.Caption + ' ' + $o.Version,
        'CPU: ' + $c.Name,
        'RAM: ' + [math]::Round($o.TotalVisibleMemorySize / 1MB, 1) + ' GB'
    )
    foreach ($g in @(Get-CimInstance Win32_VideoController)) {
        $lines += 'GPU: ' + $g.Name + '  driver ' + $g.DriverVersion + '  ' +
                  $g.CurrentHorizontalResolution + 'x' + $g.CurrentVerticalResolution
    }
    $lines | Out-File -Encoding utf8 $sysFile
} catch {
    'Could not read the PC details: ' + $_.Exception.Message | Out-File -Encoding utf8 $sysFile
}

# The settings the game ran with.
$settings = Join-Path $Root 'bin\rr6_recomp.toml'
if (Test-Path -LiteralPath $settings) {
    Copy-Item -LiteralPath $settings -Destination (Join-Path $logs 'settings-used.txt') -Force
}

# Pack it. Logs from earlier sessions are included too; they are small once zipped.
$zip = Join-Path $Root 'bug-report.zip'
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
$items = @(Get-ChildItem -LiteralPath $logs -File | ForEach-Object { $_.FullName })
$info = Join-Path $Root 'BUILD-INFO.txt'
if (Test-Path -LiteralPath $info) { $items += $info }
if ($items.Count -eq 0) { Write-Host 'Nothing to collect.'; exit 1 }
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
# "\Users\<name>" or "/Users/<name>", whatever the drive and the letter case.
$userPath = New-Object System.Text.RegularExpressions.Regex('(?<sep>[\\/])Users[\\/]+[^\\/:*?"<>|]+', 'IgnoreCase')
$archive = [System.IO.Compression.ZipFile]::Open($zip, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($item in $items) {
        try {
            # Opened shared, so a log the game still has open can be read too.
            $in = [System.IO.File]::Open($item, 'Open', 'Read', 'ReadWrite')
            try {
                $entry = $archive.CreateEntry([System.IO.Path]::GetFileName($item), [System.IO.Compression.CompressionLevel]::Optimal)
                $out = $entry.Open()
                try {
                    if ($item -match '\.(log|txt|toml)$') {
                        # Text: copied line by line, without the Windows user name.
                        $reader = New-Object System.IO.StreamReader($in, [System.Text.Encoding]::UTF8)
                        $writer = New-Object System.IO.StreamWriter($out, (New-Object System.Text.UTF8Encoding($false)))
                        while ($null -ne ($line = $reader.ReadLine())) {
                            $writer.WriteLine($userPath.Replace($line, '${sep}Users${sep}<user>'))
                        }
                        $writer.Flush()
                    } else {
                        $in.CopyTo($out)
                    }
                } finally { $out.Dispose() }
            } finally { $in.Dispose() }
        } catch {
            Write-Host ('Skipped ' + $item + ': ' + $_.Exception.Message)
        }
    }
} finally { $archive.Dispose() }
Write-Host ('Saved ' + $zip)
exit 0
