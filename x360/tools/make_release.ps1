# Builds the redistributable zips (no SEGA data inside; the user adds their own APK / data file):
#   <work folder>\release_<Version>\Sonic 1 Forever (Xbox 360).zip
#   <work folder>\release_<Version>\Sonic 2 Absolute (Xbox 360).zip
# (work folder: see paths.ps1; it also provides the Sonic 1 Forever mod and Team Forever's PS3 package "Sonic2Absolute\")
# Each zip holds the setup script, readme.txt, 7za.exe, and the game folder with default.xex (from x360\deploy, so
# build first). Sonic 2 Absolute also gets Team Forever's absolute.data (patch files, scripts and the Retrun extractor),
# repacked without the PS3's SonicAbsolute.pkg.
#
#   powershell -ExecutionPolicy Bypass -File x360\tools\make_release.ps1 -Version 1.0 [-SourceUrl https://github.com/...] [-WorkDir <folder>]
param(
	[Parameter(Mandatory = $true)][string]$Version,
	[string]$SourceUrl = "",
	[string]$WorkDir = ""
)

$ErrorActionPreference = "Stop"
. (Join-Path $PSScriptRoot "paths.ps1")
$root = Resolve-WorkDir $WorkDir
$deploy = Join-Path $RepoRoot "x360\deploy"
$templates = Join-Path $RepoRoot "x360\release"
$ps3Package = Join-Path $root "Sonic2Absolute"		# Team Forever's PS3 setup package: 7za.exe, absolute.data
$sevenZip = Join-Path $ps3Package "7za.exe"
$out = Join-Path $root "release_$Version"
if (Test-Path $out) { throw "$out already exists" }
New-Item -ItemType Directory $out | Out-Null

function Copy-Readme($source, $target) {
	$text = [System.IO.File]::ReadAllText($source)
	if ($SourceUrl -ne "") { $text = $text.Replace("<GitHub link>", $SourceUrl) }
	$text = $text.Replace("Xbox 360 port`r`n-------------", "Xbox 360 port (version $Version)`r`n---------------------------")
	[System.IO.File]::WriteAllText($target, $text, (New-Object System.Text.UTF8Encoding $false))
}

function Make-Zip($folder) {
	$zip = "$folder.zip"
	Compress-Archive -Path $folder -DestinationPath $zip
	"{0}: {1:N1} MB" -f (Split-Path $zip -Leaf), ((Get-Item $zip).Length / 1MB)
}

# ----- Sonic 1 Forever -----
$s1f = Join-Path $out "Sonic 1 Forever (Xbox 360)"
New-Item -ItemType Directory (Join-Path $s1f "Sonic1Forever") | Out-Null
Copy-Item (Join-Path $deploy "Sonic1Forever\default.xex") (Join-Path $s1f "Sonic1Forever")
robocopy (Join-Path $root "Sonic the Hedgehog Forever\mods") (Join-Path $s1f "Sonic1Forever\mods") /E /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed for the Sonic 1 Forever mod" }
Copy-Item (Join-Path $templates "Sonic1Forever\Install S1F - Xbox 360.bat") $s1f
Copy-Readme (Join-Path $templates "Sonic1Forever\readme.txt") (Join-Path $s1f "readme.txt")
Copy-Item $sevenZip $s1f

# ----- Sonic 2 Absolute -----
$s2a = Join-Path $out "Sonic 2 Absolute (Xbox 360)"
New-Item -ItemType Directory (Join-Path $s2a "Sonic2Absolute") | Out-Null
Copy-Item (Join-Path $deploy "Sonic2Absolute\default.xex") (Join-Path $s2a "Sonic2Absolute")
Copy-Item (Join-Path $templates "Sonic2Absolute\Install S2A - Xbox 360.bat") $s2a
Copy-Readme (Join-Path $templates "Sonic2Absolute\readme.txt") (Join-Path $s2a "readme.txt")
Copy-Item $sevenZip $s2a
Copy-Item (Join-Path $ps3Package "absolute.data") $s2a
& $sevenZip d (Join-Path $s2a "absolute.data") SonicAbsolute.pkg | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Removing SonicAbsolute.pkg from absolute.data failed" }

# ----- Checks: everything the setup scripts need is there, and no game data from the APKs is -----
$listing = & $sevenZip l (Join-Path $s2a "absolute.data")
foreach ($needed in @("Retrun\retrun.exe", "Retrun\RSDKv4FileList.txt", "Scripts\Players\PlayerObject.txt", "Data\Stages\MainMenu\Act1.bin")) {
	if (-not ($listing -match [regex]::Escape($needed))) { throw "absolute.data lacks $needed" }
}
if ($listing -match "SonicAbsolute\.pkg") { throw "absolute.data still contains SonicAbsolute.pkg" }
$forbidden = Get-ChildItem $out -Recurse -File | Where-Object { $_.Name -match "\.rsdk|\.apk$" -or $_.FullName -match "\\Sonic2Absolute\\(Data|Scripts)\\" }
if ($forbidden) { throw "Game data in the release: $($forbidden[0].FullName)" }

Make-Zip $s1f
Make-Zip $s2a
"Release: $out"
