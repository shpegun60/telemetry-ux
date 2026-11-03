# Qualify an extracted Windows distribution without a Qt/Python/toolchain PATH. MIT.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Archive,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$SdkRevision,
    [Parameter(Mandatory=$true)][string]$UxRevision
)
$ErrorActionPreference = 'Stop'
$archivePath = (Resolve-Path -LiteralPath $Archive).Path
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $outputPath) { throw 'A fresh output directory is required; nothing is removed' }
$expectedHash = (Get-Content -LiteralPath ($archivePath + '.sha256') -Raw).Trim().Split(' ')[0]
if ((Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $expectedHash) { throw 'ZIP hash mismatch' }
New-Item -ItemType Directory -Path $outputPath | Out-Null
$packagePath = Join-Path $outputPath 'relocated application with spaces'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archivePath)
try {
    foreach ($entry in $zip.Entries) {
        $target = [IO.Path]::GetFullPath((Join-Path $packagePath $entry.FullName))
        if (-not $target.StartsWith($packagePath + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe archive path' }
    }
} finally { $zip.Dispose() }
Expand-Archive -LiteralPath $archivePath -DestinationPath $packagePath
$identity = Get-Content -LiteralPath (Join-Path $packagePath 'BUILD-IDENTITY.json') -Raw | ConvertFrom-Json
if ($identity.sdk_revision -ne $SdkRevision -or $identity.ux_revision -ne $UxRevision) { throw 'Distribution source identity mismatch' }
$files = Get-Content -LiteralPath (Join-Path $packagePath 'FILES-SHA256.json') -Raw | ConvertFrom-Json
foreach ($file in $files.PSObject.Properties) {
    $target = [IO.Path]::GetFullPath((Join-Path $packagePath $file.Name))
    if (-not $target.StartsWith($packagePath + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe manifest path' }
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ne $file.Value) { throw ('Payload mismatch: ' + $file.Name) }
}
if (@(Get-ChildItem -LiteralPath $packagePath -Recurse -File).Count -ne @($files.PSObject.Properties).Count + 1) { throw 'Unexpected payload files' }
$exe = Join-Path $packagePath 'telemetry-inspector-qml.exe'
$environmentNames = @('PATH','QT_QPA_PLATFORM','QT_QPA_FONTDIR','QT_FORCE_STDERR_LOGGING','QT_PLUGIN_PATH','QML_IMPORT_PATH','QML2_IMPORT_PATH')
$savedEnvironment = @{}
foreach ($name in $environmentNames) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
$serverProcess = $null
$report = [ordered]@{ passed=$false; hardware_tested=$false; serial_port_opened=$false; identity=$identity; archive_sha256=$expectedHash; commands=@(); runtime_path='System32 and Windows only'; runner=$env:RUNNER_NAME }
$env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QPA_FONTDIR = "$env:SystemRoot\Fonts"
$env:QT_FORCE_STDERR_LOGGING = '1'
foreach ($name in @('QT_PLUGIN_PATH','QML_IMPORT_PATH','QML2_IMPORT_PATH')) { [Environment]::SetEnvironmentVariable($name, $null, 'Process') }

function Invoke-Inspector([string]$Label, [string[]]$InspectorArguments) {
    $stdout = Join-Path $outputPath ($Label + '.stdout.log')
    $stderr = Join-Path $outputPath ($Label + '.stderr.log')
    $quoted = @($InspectorArguments | ForEach-Object {
        if ($_.Contains('"')) { throw 'Unexpected quote in Inspector argument' }
        '"' + $_ + '"'
    })
    $process = Start-Process -FilePath $exe -ArgumentList $quoted -WorkingDirectory $packagePath -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $null = $process.Handle
    if (-not $process.WaitForExit(45000)) { Stop-Process -Id $process.Id; throw ('Inspector timeout: ' + $Label) }
    $process.Refresh()
    $text = Get-Content -LiteralPath $stdout -Raw
    $errors = Get-Content -LiteralPath $stderr -Raw
    $report.commands += @{ name=$Label; exit=$process.ExitCode; stdout=$text; stderr=$errors }
    if ($process.ExitCode -ne 0 -or $errors) { throw ('Inspector failed: ' + $Label + ' ' + $errors) }
    return $text
}

try {
    if ((Invoke-Inspector 'help' @('--help')) -notmatch 'Runtime Telemetry Inspector') { throw 'Missing help output' }
    $read = Invoke-Inspector 'read' @('--smoke','--screenshot',(Join-Path $outputPath 'read.png'))
    if ($read -notmatch 'published=1') { throw 'Actual QML FieldRead did not publish' }
    $edit = Invoke-Inspector 'edit' @('--edit-smoke','--screenshot',(Join-Path $outputPath 'edit.png'))
    if ($edit -notmatch 'writes=1 refreshed=1 gain=10') { throw 'Actual QML normalized write/readback did not complete' }
    $serverOut = Join-Path $outputPath 'tcp-server.stdout.log'
    $serverErr = Join-Path $outputPath 'tcp-server.stderr.log'
    $serverProcess = Start-Process -FilePath $exe -ArgumentList @('--serve-demo','0') -WorkingDirectory $packagePath -WindowStyle Hidden -PassThru -RedirectStandardOutput $serverOut -RedirectStandardError $serverErr
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    $port = $null
    while ([DateTime]::UtcNow -lt $deadline -and -not $serverProcess.HasExited) {
        $line = Get-Content -LiteralPath $serverOut -Raw -ErrorAction SilentlyContinue
        if ($line -match 'Inspector TCP demo listening port=(\d+)') { $port=$Matches[1]; break }
        Start-Sleep -Milliseconds 50
    }
    if (-not $port) { throw 'Packaged loopback server did not start' }
    $tcp = Invoke-Inspector 'tcp' @('--tcp',('127.0.0.1:'+$port),'--smoke','--screenshot',(Join-Path $outputPath 'tcp.png'))
    if ($tcp -notmatch 'published=1') { throw 'Packaged loopback TCP did not publish' }
    if (Get-Content -LiteralPath $serverErr -Raw) { throw 'Packaged server emitted diagnostics' }
    $report.passed = $true
} finally {
    if ($serverProcess -and -not $serverProcess.HasExited) { Stop-Process -Id $serverProcess.Id }
    foreach ($name in $environmentNames) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
    $report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $outputPath 'qualification.json') -Encoding UTF8
}
