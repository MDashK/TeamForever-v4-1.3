# Copies the game data next to the built XEX files, so that x360\deploy\<Game>\ is a complete game folder
# (for Xenia, and to copy to the console as is).
#
#   powershell -ExecutionPolicy Bypass -File x360\tools\deploy_data.ps1 [-Game Sonic1Forever|Sonic2Absolute|All] [-WorkDir <folder>]
#
# The data comes from the work folder (see paths.ps1):
# Sonic 1 Forever:  "Sonic the Hedgehog Forever\": Data.rsdk (the original game's data pack) + mods\ (the "Sonic 1 Forever" mod)
# Sonic 2 Absolute: "Sonic the Hedgehog 2 Absolute\": Data\ + Scripts\ (loose files, as made by its setup script) + mods\
# Existing files in the deploy folders (default.xex, settings.ini, saves, logs) are kept.
param(
	[ValidateSet("Sonic1Forever", "Sonic2Absolute", "All")][string]$Game = "All",
	[string]$WorkDir = ""
)

. (Join-Path $PSScriptRoot "paths.ps1")
$root = Resolve-WorkDir $WorkDir
$deploy = Join-Path $RepoRoot "x360\deploy"

$games = @{
	"Sonic1Forever"  = @{ Source = "Sonic the Hedgehog Forever";    Items = @("Data.rsdk", "mods") }
	"Sonic2Absolute" = @{ Source = "Sonic the Hedgehog 2 Absolute"; Items = @("Data", "Scripts", "mods") }
}

foreach ($name in $games.Keys) {
	if ($Game -ne "All" -and $Game -ne $name) { continue }
	$source = Join-Path $root $games[$name].Source
	$target = Join-Path $deploy $name
	New-Item -ItemType Directory -Force $target | Out-Null
	foreach ($item in $games[$name].Items) {
		$path = Join-Path $source $item
		if (Test-Path $path -PathType Container) {
			robocopy $path (Join-Path $target $item) /E /NFL /NDL /NJH /NJS /NP | Out-Null
			if ($LASTEXITCODE -ge 8) { throw "robocopy failed for $path ($LASTEXITCODE)" }
		} elseif (Test-Path $path) {
			Copy-Item $path $target -Force
		} else {
			Write-Warning "Missing game data: $path"
		}
	}
	"$name -> $target"
}
