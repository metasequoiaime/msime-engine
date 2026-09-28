[CmdletBinding()]
param([switch]$Force)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
$modelDir = Join-Path $root 'neural-model'
$lock = Get-Content (Join-Path $modelDir 'lock.json') -Raw | ConvertFrom-Json

foreach ($model in $lock.models) {
    $destination = Join-Path $modelDir $model.file
    if ((Test-Path $destination) -and -not $Force) {
        $hash = (Get-FileHash $destination -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -eq $model.sha256.ToLowerInvariant()) {
            Write-Host "Validated $($model.file)"
            continue
        }
    }
    Invoke-WebRequest -Uri $model.url -OutFile $destination
    $hash = (Get-FileHash $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ne $model.sha256.ToLowerInvariant()) {
        throw "SHA-256 mismatch for $($model.file): expected $($model.sha256), got $hash"
    }
}
Write-Host 'Neural sentence models are ready.'
