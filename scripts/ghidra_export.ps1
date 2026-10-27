param([Parameter(Mandatory=$true)][string]$GhidraHome)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$headless = Join-Path $GhidraHome 'support\analyzeHeadless.bat'
if (-not (Test-Path -LiteralPath $headless)) { throw 'Ghidra headless installation not found' }
$elfPath = Join-Path $projectRoot 'private\disc\extracted\SLES_522.56'
if ((Get-FileHash -LiteralPath $elfPath -Algorithm SHA256).Hash -ne '9DAC1FEC5EB742E123CEE20BF141BF7C05E43D1FCF6DE9B69750A4E7720BEDBA') {
    throw 'Wrong PAL ELF: refusing address analysis'
}
New-Item -ItemType Directory -Force (Join-Path $projectRoot 'private\ghidra') | Out-Null
# Install the pinned Emotion Engine extension in this Ghidra version first.
# ExportPS2Functions currently prompts for output files; use its GUI exporter
# only in an ordinary window, or extend it with explicit headless arguments.
& $headless (Join-Path $projectRoot 'private\ghidra') MP2R -import $elfPath -processor 'r5900:LE:32:default' -analysisTimeoutPerFile 300 -max-cpu 2 -log (Join-Path $projectRoot 'private\ghidra\analysis.log')
exit $LASTEXITCODE
