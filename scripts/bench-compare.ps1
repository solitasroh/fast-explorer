<#
.SYNOPSIS
  Compares two FastExplorerBench enumerate JSON results and reports
  whether the current run regressed against a baseline beyond a
  tolerance plus the §14.7 absolute gates.

.DESCRIPTION
  Inputs are the JSON documents produced by:
    FastExplorerBench.exe enumerate --path <dataset> --runs N --format json

  Comparison rules:
    timing.median_us / timing.p95_us
        FAIL when current > baseline * (1 + tolerance/100)
    memory.working_set.peak_bytes
        FAIL when current > baseline * (1 + tolerance/100)
    memory.working_set.max_cycle_drift_bytes
        FAIL when current > 5 MB (the §14.7 absolute gate);
        baseline value is informational.

  Exit code:
    0 — PASS (all checks within tolerance and gate)
    1 — REGRESSION (at least one FAIL row)
    2 — INPUT ERROR (file missing, JSON parse error, schema mismatch)

  Schema requirements (both files):
    machine.{architecture, processor_count, page_size, os.{major,minor,build}}
    args.{path, runs}
    timing.{median_us, p95_us, total_entries, runs[]}
    memory.{last_run_entries_bytes, last_run_arena_committed_bytes,
            working_set.{baseline_bytes, peak_bytes, final_bytes,
                         max_cycle_drift_bytes, post_cycle_bytes[]}}

.PARAMETER Baseline
  Path to the baseline JSON file.

.PARAMETER Current
  Path to the current-run JSON file.

.PARAMETER TolerancePercent
  Allowed regression in percent (default 5). Applied to timing and
  working-set peak.

.EXAMPLE
  pwsh -File scripts/bench-compare.ps1 `
       -Baseline runbooks/baseline-100k.json `
       -Current build/bench-current.json `
       -TolerancePercent 10
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$Baseline,

    [Parameter(Mandatory=$true)]
    [string]$Current,

    [int]$TolerancePercent = 5
)

$ErrorActionPreference = 'Stop'

function Read-BenchJson {
    param([string]$Path, [string]$Label)
    if (-not (Test-Path -LiteralPath $Path)) {
        Write-Error "$Label file not found: $Path"
        exit 2
    }
    try {
        return Get-Content -LiteralPath $Path -Raw -Encoding utf8 |
               ConvertFrom-Json
    } catch {
        Write-Error "Failed to parse $Label JSON ($Path): $_"
        exit 2
    }
}

function Assert-Path {
    param($Object, [string[]]$Chain, [string]$Label)
    $cursor = $Object
    foreach ($key in $Chain) {
        if ($null -eq $cursor -or -not ($cursor.PSObject.Properties.Name -contains $key)) {
            Write-Error "$Label JSON is missing required path: $($Chain -join '.')"
            exit 2
        }
        $cursor = $cursor.$key
    }
    return $cursor
}

function Format-Percent {
    param([double]$Ratio)
    return "{0:+0.00;-0.00;0.00}%" -f ($Ratio * 100)
}

function Compare-Field {
    param(
        [string]$Field,
        [uint64]$BaselineValue,
        [uint64]$CurrentValue,
        [double]$TolerancePercent
    )
    $row = [PSCustomObject]@{
        Field    = $Field
        Baseline = $BaselineValue
        Current  = $CurrentValue
        Delta    = $null
        Status   = 'PASS'
    }
    if ($BaselineValue -eq 0) {
        $row.Delta = '(baseline=0)'
        if ($CurrentValue -gt 0) {
            $row.Status = 'WARN'
        }
        return $row
    }
    $ratio = ($CurrentValue - $BaselineValue) / [double]$BaselineValue
    $row.Delta = Format-Percent $ratio
    $threshold = $TolerancePercent / 100.0
    if ($ratio -gt $threshold) {
        $row.Status = 'FAIL'
    }
    return $row
}

# -----------------------------------------------------------------
# Load and validate both documents.

$base = Read-BenchJson -Path $Baseline -Label 'baseline'
$curr = Read-BenchJson -Path $Current  -Label 'current'

# Cheap schema check — just probe the keys we will read.
[void](Assert-Path $base @('timing','median_us') 'baseline')
[void](Assert-Path $base @('timing','p95_us') 'baseline')
[void](Assert-Path $base @('memory','working_set','peak_bytes') 'baseline')
[void](Assert-Path $curr @('timing','median_us') 'current')
[void](Assert-Path $curr @('timing','p95_us') 'current')
[void](Assert-Path $curr @('memory','working_set','peak_bytes') 'current')

# -----------------------------------------------------------------
# Comparisons.

$rows = @()
$rows += Compare-Field 'timing.median_us' `
    ([uint64]$base.timing.median_us) ([uint64]$curr.timing.median_us) $TolerancePercent
$rows += Compare-Field 'timing.p95_us' `
    ([uint64]$base.timing.p95_us) ([uint64]$curr.timing.p95_us) $TolerancePercent
$rows += Compare-Field 'memory.working_set.peak_bytes' `
    ([uint64]$base.memory.working_set.peak_bytes) `
    ([uint64]$curr.memory.working_set.peak_bytes) $TolerancePercent

# §14.7 absolute gate: working_set.max_cycle_drift_bytes ≤ 5 MB.
$gateBytes = 5 * 1024 * 1024
$driftRow = [PSCustomObject]@{
    Field    = 'memory.working_set.max_cycle_drift_bytes'
    Baseline = [uint64]$base.memory.working_set.max_cycle_drift_bytes
    Current  = [uint64]$curr.memory.working_set.max_cycle_drift_bytes
    Delta    = "(gate ≤ $gateBytes B)"
    Status   = if ([uint64]$curr.memory.working_set.max_cycle_drift_bytes -gt $gateBytes) { 'FAIL' } else { 'PASS' }
}
$rows += $driftRow

# -----------------------------------------------------------------
# Report.

Write-Host ""
Write-Host "Baseline: $Baseline"
Write-Host "Current : $Current"
Write-Host "Tolerance: $TolerancePercent%"
Write-Host ""

$rows | Format-Table -AutoSize Field, Baseline, Current, Delta, Status

$failCount = ($rows | Where-Object { $_.Status -eq 'FAIL' }).Count
$warnCount = ($rows | Where-Object { $_.Status -eq 'WARN' }).Count

if ($failCount -gt 0) {
    Write-Host ""
    Write-Host "REGRESSION: $failCount FAIL row(s)." -ForegroundColor Red
    exit 1
}

if ($warnCount -gt 0) {
    Write-Host ""
    Write-Host "PASS with $warnCount WARN row(s)." -ForegroundColor Yellow
} else {
    Write-Host ""
    Write-Host "PASS: all checks within tolerance and gate." -ForegroundColor Green
}
exit 0
