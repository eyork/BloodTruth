# Share the shader cache: one player exports what their playthrough compiled, another imports it so those
# pipelines are built at boot instead of the first time each scene is seen.
#   .\shader-cache.ps1 -Export bloodtruth-shaders.zip
#   .\shader-cache.ps1 -Import bloodtruth-shaders.zip
# A cache only works on a GPU and driver that translate shaders the same way (same profile.bin, usually the
# same GPU family) and with the same SHADPS4_WAVE64* / SHADPS4_WWM* settings it was made with. On another GPU
# the emulator notices and starts a fresh cache.
param([string]$Export, [string]$Import)
$ErrorActionPreference='Stop'
$cacheDir=Join-Path $PSScriptRoot 'user/cache'
$gameCache=Join-Path $cacheDir 'CUSA11108'
$stampPath=Join-Path $cacheDir 'shader-settings.stamp'
if ([bool]$Export -eq [bool]$Import) { throw 'Give either -Export <file.zip> or -Import <file.zip>.' }
if (Get-Process shadps4 -ErrorAction SilentlyContinue) { throw 'Close the running emulator first.' }

function Settings([string]$stampFile) {
 if (-not (Test-Path -LiteralPath $stampFile)) { return @() }
 @((Get-Content -LiteralPath $stampFile) | Where-Object { $_ -like 'SHADPS4_*' })
}
function Version([string]$stampFile) {
 if (-not (Test-Path -LiteralPath $stampFile)) { return '' }
 $first=@(Get-Content -LiteralPath $stampFile)[0]
 # Stamps written before the cache version began with the exe's hash.
 if ($first -match '^[0-9A-F]{64}$') { 'shader-cache=1' } else { $first }
}
function Count([string]$dir, [string]$filter) { @(Get-ChildItem -LiteralPath $dir -Filter $filter -File -ErrorAction SilentlyContinue).Count }

if ($Export) {
 if (-not (Test-Path -LiteralPath (Join-Path $gameCache 'profile.bin'))) { throw 'There is no shader cache yet: play with the launcher first.' }
 $Export=[IO.Path]::GetFullPath($Export)
 if (Test-Path -LiteralPath $Export) { Remove-Item -LiteralPath $Export }
 Push-Location $cacheDir
 try { tar -a -cf $Export 'CUSA11108' 'shader-settings.stamp'; if ($LASTEXITCODE) { throw 'tar failed.' } } finally { Pop-Location }
 $settings=Settings $stampPath
 "Exported $(Count $gameCache '*.key') pipelines, $(Count $gameCache '*.spv') shaders: $Export ($([math]::Round((Get-Item -LiteralPath $Export).Length/1MB)) MB)"
 "Made with: $(if ($settings) { $settings -join ', ' } else { 'default culling settings' })"
 return
}

$Import=[IO.Path]::GetFullPath($Import)
if (-not (Test-Path -LiteralPath $Import)) { throw "Not found: $Import" }
$staging=Join-Path $PSScriptRoot 'user/cache-import'
if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
New-Item -ItemType Directory -Force $staging | Out-Null
try {
 tar -xf $Import -C $staging; if ($LASTEXITCODE) { throw 'tar failed.' }
 $newCache=Join-Path $staging 'CUSA11108'
 $newStamp=Join-Path $staging 'shader-settings.stamp'
 if (-not (Test-Path -LiteralPath (Join-Path $newCache 'profile.bin')) -or -not (Test-Path -LiteralPath $newStamp)) { throw 'This is not a Blood & Truth shader cache export.' }

 # Same GPU profile and settings: add the new pipelines to ours. Otherwise keep ours aside and use theirs.
 $ownProfile=Join-Path $gameCache 'profile.bin'
 $merge=(Test-Path -LiteralPath $ownProfile) -and
  ((Get-FileHash -LiteralPath $ownProfile).Hash -eq (Get-FileHash -LiteralPath (Join-Path $newCache 'profile.bin')).Hash) -and
  ((Settings $stampPath) -join "`n") -eq ((Settings $newStamp) -join "`n") -and
  (Version $stampPath) -eq (Version $newStamp)
 if ($merge) {
  $before=Count $gameCache '*.key'
  Get-ChildItem -LiteralPath $newCache -File | Where-Object { -not (Test-Path -LiteralPath (Join-Path $gameCache $_.Name)) } |
   Move-Item -Destination $gameCache
  "Added $((Count $gameCache '*.key') - $before) pipelines to your cache ($(Count $gameCache '*.key') now)."
 } else {
  if (Test-Path -LiteralPath $gameCache) {
   $backup=Join-Path $PSScriptRoot ('user/cache-backup-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
   Move-Item -LiteralPath $cacheDir -Destination $backup
   "Your previous cache was made with another GPU or other settings; it is kept in $backup"
  }
  New-Item -ItemType Directory -Force $cacheDir | Out-Null
  Move-Item -LiteralPath $newCache -Destination $gameCache
  Copy-Item -LiteralPath $newStamp -Destination $stampPath -Force
  "Imported $(Count $gameCache '*.key') pipelines."
 }
 $launcherVersion=[regex]::Match((Get-Content -LiteralPath (Join-Path $PSScriptRoot 'launch-bloodtruth.ps1') -Raw), "shaderCacheVersion='([^']+)'").Groups[1].Value
 if ((Version $newStamp) -ne $launcherVersion) { "Warning: this cache is from another emulator version ($(Version $newStamp), this launcher uses $launcherVersion); the launcher will clear it." }
 $settings=Settings $newStamp
 "Start the game with the same culling settings the cache was made with, or the launcher clears it:"
 if ($settings) { $settings | ForEach-Object { '  $env:' + ($_ -replace '=', " = '") + "'" } } else { '  (default culling settings: no SHADPS4_WAVE64* variables)' }
} finally {
 Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
}
