# Builds a folder ready to copy to the console (FTP to e.g. Hdd1:\Games\), with one game folder per XEX:
#   <work folder>\<Name>\Sonic1Forever\  and  <work folder>\<Name>\Sonic2Absolute\   (work folder: see paths.ps1)
# from x360\deploy (run build.cmd and deploy_data.ps1 first). Leaves out what only matters on the PC or was
# produced by test runs: default.elf (symbols), logs, audio dumps, settings and saves.
#
#   powershell -ExecutionPolicy Bypass -File x360\tools\make_package.ps1 -Name hardware_test_v1 [-Notes <text file>] [-WorkDir <folder>]
param(
	[Parameter(Mandatory = $true)][string]$Name,
	[string]$Notes = "",
	[string]$WorkDir = ""
)

. (Join-Path $PSScriptRoot "paths.ps1")
$root = Resolve-WorkDir $WorkDir
$deploy = Join-Path $RepoRoot "x360\deploy"
$target = Join-Path $root $Name
if (Test-Path $target) { throw "$target already exists" }

$exclude = @("default.elf", "rsdkv4.log", "audio_dump.wav", "dump_*.txt", "settings.ini", "SData.bin", "SGame.bin", "UData.bin")
foreach ($game in @("Sonic1Forever", "Sonic2Absolute")) {
	$source = Join-Path $deploy $game
	if (-not (Test-Path (Join-Path $source "default.xex"))) { throw "Missing $source\default.xex (build first)" }
	robocopy $source (Join-Path $target $game) /E /XF $exclude /NFL /NDL /NJH /NJS /NP | Out-Null
	if ($LASTEXITCODE -ge 8) { throw "robocopy failed for $game ($LASTEXITCODE)" }
	# Every file of the original game data must be in the package (v1 lost all WAV sound effects to an exclude pattern)
	$dataSource = Join-Path $root @{ "Sonic1Forever" = "Sonic the Hedgehog Forever"; "Sonic2Absolute" = "Sonic the Hedgehog 2 Absolute" }[$game]
	$missing = Get-ChildItem $dataSource -Recurse -File | Where-Object { $_.Name -ne "Forever.ini" } |
		Where-Object { -not (Test-Path (Join-Path (Join-Path $target $game) $_.FullName.Substring($dataSource.Length + 1))) }
	if ($missing) { throw "$game package is missing $(@($missing).Count) data files, e.g. $($missing[0].FullName)" }
	$xex = Get-Item (Join-Path $target "$game\default.xex")
	"{0}: default.xex {1:N0} bytes, {2}" -f $game, $xex.Length, $xex.LastWriteTime
}
if ($Notes -ne "") { Copy-Item $Notes (Join-Path $target "LEIA-ME.txt") }
"Package: $target"
