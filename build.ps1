# build.ps1 -- build assassinscreed_nx.nsp in the AArch32 toolchain container, from
# Windows PowerShell. The same mounts as runtime/tools/docker_build.sh: this
# folder at /work, the patched libnx32 (../libnx32/prefix) over the image's.
#   .\build.ps1                  # assassinscreed_nx.nsp and assassinscreed_nx.build
#   .\build.ps1 clean
#   .\build.ps1 rt-files         # what is built from where
param([Parameter(ValueFromRemainingArguments = $true)] [string[]] $MakeArgs)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$nx32 = Join-Path (Split-Path -Parent $here) 'libnx32\prefix'
if (-not (Test-Path (Join-Path $nx32 'lib\libnx.a'))) {
  throw "libnx32 is not built at $nx32 (run its build first)"
}
$image = if ($env:DCR_TOOLCHAIN_IMAGE) { $env:DCR_TOOLCHAIN_IMAGE } else { 'ghcr.io/vita2hos/devcontainer/vita2hos:latest' }
$nxd = '/opt/devkitpro/libnx32'
$dockerArgs = @(
  'run', '--rm', '--platform', 'linux/amd64',
  '-v', "${here}:/work", '-w', '/work',
  '-v', "$nx32\include\switch:$nxd/include/switch:ro",
  '-v', "$nx32\include\switch.h:$nxd/include/switch.h:ro",
  '-v', "$nx32\lib\libnx.a:$nxd/lib/libnx.a:ro",
  '-v', "$nx32\lib\libnxd.a:$nxd/lib/libnxd.a:ro",
  $image, 'bash', '-lc', 'exec make -j"$(nproc)" "$@"', 'make'
) + $MakeArgs
# the compiler's messages arrive on stderr: show them, do not stop on them
$ErrorActionPreference = 'Continue'
& docker @dockerArgs 2>&1 | ForEach-Object { "$_" }
exit $LASTEXITCODE
