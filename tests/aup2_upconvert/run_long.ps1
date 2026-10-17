# Runs the 30/60 minute retime drift test headless (plain python -I, no AviUtl host needed).
# Temporary files go to $env:AUP2_TEST_TMP when set, otherwise to a folder under %TEMP%.
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $env:AUP2_TEST_TMP) { $env:AUP2_TEST_TMP = Join-Path $env:TEMP 'aup2_long_duration' }
& python -I (Join-Path $here 'test_long_duration.py')
exit $LASTEXITCODE
