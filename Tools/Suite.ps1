# The regression suite: selftest, an idle and a combat run on Cathedral, and a
# fight against each of the six bosses, all headless and repeatable. Writes one
# log per run and a summary to -Out; with -Baseline, compares against an
# earlier -Out and lists what moved. Exits 1 when a run crashes, a script
# errors or a check fails.
#
#   Tools\Suite.ps1 -Out Build\Suite\before
#   Tools\Suite.ps1 -Out Build\Suite\after -Baseline Build\Suite\before
#   Tools\Suite.ps1 -Only boss-Giant,boss-Swamp
#
# Docs/Reference/Diagnostics.md, "The suite".
param(
	[string]$Out = "",
	[string]$Baseline = "",
	[string[]]$Only = @(),
	[string]$Exe = "",
	[string]$Data = ""
)

$repo = Split-Path $PSScriptRoot
if (-not $Out) { $Out = Join-Path $repo "Build\Suite\current" }
if (-not $Exe) { $Exe = Join-Path $repo "Build\Bin\Release\PainfulTools.exe" }
if (-not $Data) { $Data = Join-Path (Split-Path $repo) "Data" }
if (-not (Test-Path $Exe)) { Write-Output "suite: no executable at $Exe"; exit 2 }
if (-not (Test-Path $Data)) { Write-Output "suite: no data at $Data"; exit 2 }
New-Item -ItemType Directory -Force $Out | Out-Null

$combat = Get-Content (Join-Path $PSScriptRoot "Suite\Combat.lua") -Raw
$boss = Get-Content (Join-Path $PSScriptRoot "Suite\Boss.lua") -Raw

# name, level, frames, exec chunk
$runs = @(
	@("idle-C1L1", "C1L1_Cathedral", 400, ""),
	@("combat-C1L1", "C1L1_Cathedral", 900, $combat)
)
# boss template, level, and what starts the fight when the level does not on
# its own: Hell holds Lucifer back until 66 kills, then calls ToLucifer.
foreach ($b in @(@("Giant", "C1L5_Enclave", ""), @("Swamp", "C2L6_Swamp", ""), @("Thor", "C3L5_Ruins", ""),
		@("Alastor", "C4L4_Alastor", ""),
		@("Lucifer", "C5L4_Hell", "Lev._kills = Game.BodyCountTotal Lev:ToLucifer()"),
		@("Panzer_Spider", "C6L4_Pentagon", ""))) {
	$chunk = "BOSS='" + $b[0] + "'`n" + $combat + "`n" + $b[2] + "`n" + $boss
	$runs += , @(("boss-" + $b[0]), $b[1], 3600, $chunk)
}

function Summarise($name, $file, $code) {
	$text = Get-Content $file
	$s = [ordered]@{ name = $name; exit = $code; errors = "-"; stubs = "-"; calls = "-"; checks = "-"; lines = $text.Count }
	foreach ($l in $text) {
		if ($l -match '^boot: .* (\d+) script errors') { $s.errors = [int]$Matches[1] }
		elseif ($l -match '^unimplemented natives hit: (\d+) distinct, (\d+) calls') { $s.stubs = [int]$Matches[1]; $s.calls = [int]$Matches[2] }
		elseif ($l -match '^checks: none failed') { $s.checks = 0 }
		elseif ($l -match '^checks: (\d+)') { $s.checks = [int]$Matches[1] }
		elseif ($l -match '^selftest: (\d+) checks passed') { $s.checks = 0 }
		elseif ($l -match '^selftest: (\d+) of') { $s.checks = [int]$Matches[1] }
	}
	return $s
}

# The stub table at the end of a lua run: "   <count>  MODULE.Name".
function StubTable($file) {
	$t = @{}
	$in = $false
	foreach ($l in (Get-Content $file)) {
		if ($l -match '^unimplemented natives hit') { $in = $true; continue }
		if ($in -and $l -match '^\s+(\d+)\s+(\S+)$') { $t[$Matches[2]] = [int]$Matches[1] }
		elseif ($in) { $in = $false }
	}
	return $t
}

$results = @()
$failed = $false
$all = @(, @("selftest", "", 0, "")) + $runs
foreach ($r in $all) {
	$name = $r[0]
	if ($Only.Count -and $Only -notcontains $name) { continue }
	$file = Join-Path $Out "$name.txt"
	if ($name -eq "selftest") {
		& $Exe selftest > $file 2>&1
	} elseif ($r[3]) {
		& $Exe lua $Data $r[2] $r[1] $r[3] > $file 2>&1
	} else {
		& $Exe lua $Data $r[2] $r[1] > $file 2>&1
	}
	$s = Summarise $name $file $LASTEXITCODE
	if ($s.exit -ne 0 -or ($s.checks -ne 0 -and $s.checks -ne "-")) { $failed = $true }
	$results += , $s
}

$summary = @("{0,-20} {1,5} {2,6} {3,6} {4,7} {5,6} {6,6}" -f "run", "exit", "errors", "stubs", "calls", "checks", "lines")
foreach ($s in $results) {
	$summary += "{0,-20} {1,5} {2,6} {3,6} {4,7} {5,6} {6,6}" -f $s.name, $s.exit, $s.errors, $s.stubs, $s.calls, $s.checks, $s.lines
}
$summary | Set-Content (Join-Path $Out "summary.txt") -Encoding utf8
$summary | Write-Output

if ($Baseline) {
	Write-Output ""
	Write-Output "against $Baseline"
	foreach ($s in $results) {
		$old = Join-Path $Baseline "$($s.name).txt"
		$new = Join-Path $Out "$($s.name).txt"
		if (-not (Test-Path $old)) { Write-Output ("  {0}: no baseline" -f $s.name); continue }
		$diff = @(Compare-Object (Get-Content $old) (Get-Content $new))
		if ($diff.Count -eq 0) { Write-Output ("  {0}: identical" -f $s.name); continue }
		Write-Output ("  {0}: {1} lines differ" -f $s.name, $diff.Count)
		$a = StubTable $old
		$b = StubTable $new
		foreach ($k in ($a.Keys + $b.Keys | Sort-Object -Unique)) {
			$x = if ($a.ContainsKey($k)) { $a[$k] } else { 0 }
			$y = if ($b.ContainsKey($k)) { $b[$k] } else { 0 }
			if ($x -eq 0) { Write-Output ("      new stub   {0} ({1})" -f $k, $y) }
			elseif ($y -eq 0) { Write-Output ("      gone       {0} ({1})" -f $k, $x) }
		}
	}
}

if ($failed) { Write-Output "suite: FAILED"; exit 1 }
Write-Output "suite: passed"
exit 0
