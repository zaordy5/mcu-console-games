param([switch]$PrepareOnly)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$version = '1.17.0'
$package = "renode-$version.windows-portable.zip"
$download = "https://github.com/renode/renode/releases/download/v$version/$package"
$expectedHash = '18bcf145422039e8702c3e5f9864c7787561bfd5deb7a3ee6e168ecf19e2b9db'
$cacheRoot = Join-Path $env:LOCALAPPDATA 'SaperRenode'
$installed = Join-Path $cacheRoot $version

function Find-Renode([string]$Folder) {
    if (Test-Path -LiteralPath $Folder -PathType Container) {
        Get-ChildItem -LiteralPath $Folder -Filter 'renode.exe' -File -Recurse |
            Select-Object -First 1
    }
}

function Join-ArchiveParts([string]$Archive, [string[]]$Parts) {
    $output = [IO.File]::Create($Archive)
    try {
        foreach ($part in $Parts) {
            $inputStream = [IO.File]::OpenRead($part)
            try {
                $inputStream.CopyTo($output)
            } finally {
                $inputStream.Dispose()
            }
        }
    } finally {
        $output.Dispose()
    }
}

try {
    $renode = Find-Renode $installed
    if (-not $renode) {
        New-Item -ItemType Directory -Path $cacheRoot -Force | Out-Null
        $work = Join-Path $cacheRoot ([Guid]::NewGuid().ToString('N'))
        $unpacked = Join-Path $work 'unpacked'
        New-Item -ItemType Directory -Path $work | Out-Null
        try {
            $archive = Join-Path $PSScriptRoot $package
            if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
                $parts = @("$archive.001", "$archive.002")
                $archive = Join-Path $work $package
                if ((Test-Path -LiteralPath $parts[0] -PathType Leaf) -and
                    (Test-Path -LiteralPath $parts[1] -PathType Leaf)) {
                    Write-Host "Joining the two local Renode archive parts..."
                    Join-ArchiveParts $archive $parts
                } else {
                    Write-Host "Downloading Renode $version from GitHub..."
                    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
                    Invoke-WebRequest -UseBasicParsing -Uri $download -OutFile $archive
                }
            }
            Write-Host "Checking and unpacking Renode $version..."
            if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expectedHash) {
                throw "Renode archive checksum mismatch. Download a fresh copy of $package."
            }
            Expand-Archive -LiteralPath $archive -DestinationPath $unpacked
            if (-not (Find-Renode $unpacked)) {
                throw 'The Renode executable was missing from the verified archive.'
            }
            try {
                [IO.Directory]::Move($unpacked, $installed)
            } catch {
                if (-not (Find-Renode $installed)) {
                    throw
                }
            }
        } finally {
            Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
        }
        $renode = Find-Renode $installed
    }

    if ($PrepareOnly) {
        $renode.FullName
        exit 0
    }

    foreach ($file in @('saper.resc', 'games.elf')) {
        if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $file) -PathType Leaf)) {
            throw "Missing $file next to Start-Saper.cmd."
        }
    }
    Start-Process -FilePath $renode.FullName -WorkingDirectory $PSScriptRoot -ArgumentList @('saper.resc','-e','start')
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
