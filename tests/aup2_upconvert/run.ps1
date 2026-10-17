# Runs the aup2 up-convert tests headless (plain python -I, no pytest needed).
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
& python -I (Join-Path $here 'test_upconvert.py')
exit $LASTEXITCODE
