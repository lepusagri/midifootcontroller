$projectRoot = Split-Path -Parent $PSScriptRoot
$previewPath = Join-Path $projectRoot 'docs\preview\index.html'
$previewUrl = ([System.Uri]$previewPath).AbsoluteUri
$chromePaths = @(
  'C:\Program Files\Google\Chrome\Application\chrome.exe',
  'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe',
  'C:\Program Files\Microsoft\Edge\Application\msedge.exe'
)
$browser = $chromePaths | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $browser) { throw 'Chrome oder Edge wurde nicht gefunden.' }

python (Join-Path $PSScriptRoot 'generate_web_preview.py')

foreach ($view in @('spielen', 'presets', 'custom-midi', 'verwaltung')) {
  $target = Join-Path $projectRoot "docs\preview\$view-desktop.png"
  & $browser --headless=new --disable-gpu --no-first-run --disable-extensions --hide-scrollbars --allow-file-access-from-files --window-size=1440,1000 --virtual-time-budget=3000 --screenshot=$target "$previewUrl#$view"
  if (-not (Test-Path -LiteralPath $target)) { throw "Screenshot fehlt: $target" }
}

$mobileTarget = Join-Path $projectRoot 'docs\preview\spielen-mobil.png'
& $browser --headless=new --disable-gpu --no-first-run --disable-extensions --hide-scrollbars --allow-file-access-from-files --window-size=500,900 --virtual-time-budget=3000 --screenshot=$mobileTarget "$previewUrl#spielen"
if (-not (Test-Path -LiteralPath $mobileTarget)) { throw "Screenshot fehlt: $mobileTarget" }
