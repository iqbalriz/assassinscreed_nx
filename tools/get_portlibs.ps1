# get_portlibs.ps1 -- put the AArch32 Mesa build (mesa32's lib/ and include/)
# into ..\portlibs32, where the Makefile looks for the renderer. Docker cannot
# follow a link out of the mounted project, so it is a copy.
#
# By default it downloads mesa32's release (https://github.com/aks796/mesa32,
# about 5 MB). If you built mesa32 yourself, point MESA32 at its prefix folder
# and nothing is downloaded:
#   $env:MESA32 = "C:\path\to\mesa32\prefix"; .\tools\get_portlibs.ps1
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$dest = Join-Path $here 'portlibs32'

if ($env:MESA32) {
  $src = $env:MESA32
} else {
  $url = 'https://github.com/aks796/mesa32/releases/download/release/mesa32.zip'
  $tmp = Join-Path ([IO.Path]::GetTempPath()) ('mesa32_' + [guid]::NewGuid().ToString('N'))
  New-Item -ItemType Directory -Force $tmp | Out-Null
  $zip = Join-Path $tmp 'mesa32.zip'
  Write-Host "downloading $url"
  Invoke-WebRequest $url -OutFile $zip
  Expand-Archive $zip -DestinationPath $tmp -Force
  $src = Join-Path $tmp 'mesa32'
}
if (-not (Test-Path (Join-Path $src 'lib\libEGL.a'))) {
  throw "no Mesa build at $src (lib\libEGL.a is missing)"
}
New-Item -ItemType Directory -Force $dest | Out-Null
Copy-Item (Join-Path $src 'lib') $dest -Recurse -Force
Copy-Item (Join-Path $src 'include') $dest -Recurse -Force
Write-Host "portlibs32\ <- $src"
