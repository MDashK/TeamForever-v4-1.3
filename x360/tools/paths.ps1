# Paths shared by the x360\tools scripts (dot-source it: . (Join-Path $PSScriptRoot "paths.ps1")).
#
# $RepoRoot: the repository (engine source + x360\). Builds go to $RepoRoot\x360\deploy\<Game>\.
# $WorkDir:  the work folder, outside the repository, that holds what can't be in it: the game data
#            ("Sonic the Hedgehog Forever\", "Sonic the Hedgehog 2 Absolute\"), Team Forever's PS3 setup package for
#            Sonic 2 Absolute ("Sonic2Absolute\": absolute.data, 7za.exe), Xenia ("xenia_canary_windows\"), and the
#            hardware test packages and releases made by the tools.
#            By default the folder that contains the repository; override with -WorkDir or the environment
#            variable RSDKV4_X360_WORKDIR.

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

function Resolve-WorkDir([string]$WorkDir) {
	if ($WorkDir -eq "") { $WorkDir = $env:RSDKV4_X360_WORKDIR }
	if ($WorkDir -eq "" -or $null -eq $WorkDir) { $WorkDir = Split-Path $RepoRoot -Parent }
	return (Resolve-Path $WorkDir).Path
}
