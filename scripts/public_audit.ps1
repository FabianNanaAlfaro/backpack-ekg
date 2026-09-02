$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$excluded = @('\\.git\\', '\\site\\assets\\')
$files = Get-ChildItem -LiteralPath $repoRoot -Recurse -File | Where-Object {
    $path = $_.FullName
    -not ($excluded | Where-Object { $path -match $_ })
}

$blockedExtensions = @('.csv', '.dat', '.cal', '.3d', '.pbix', '.edf', '.mat', '.h5', '.mp4', '.mov', '.avi', '.zip')
$badFiles = @($files | Where-Object { $blockedExtensions -contains $_.Extension.ToLowerInvariant() })
if ($badFiles.Count -gt 0) {
    $badFiles | ForEach-Object { Write-Error "Blocked artifact in public tree: $($_.FullName)" }
}

$secretPatterns = @(
    'BEGIN (RSA |EC |OPENSSH )?PRIVATE KEY',
    '(?i)(api[_-]?key|access[_-]?token|client[_-]?secret)\s*[:=]\s*["'']?[A-Za-z0-9_\-]{16,}',
    '(?i)password\s*[:=]\s*["'']?[^\s"'']{8,}'
)
$hits = foreach ($file in $files) {
    if ($file.Length -gt 2MB) { continue }
    $content = Get-Content -LiteralPath $file.FullName -Raw -ErrorAction SilentlyContinue
    foreach ($pattern in $secretPatterns) {
        if ($content -match $pattern) {
            [pscustomobject]@{ File = $file.FullName; Pattern = $pattern }
        }
    }
}
if ($hits.Count -gt 0) {
    $hits | Format-Table -AutoSize | Out-String | Write-Error
}

if ($badFiles.Count -gt 0 -or $hits.Count -gt 0) {
    exit 1
}

Write-Output "PUBLIC_AUDIT_OK: $($files.Count) files inspected"

