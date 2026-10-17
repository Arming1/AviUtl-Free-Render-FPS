# Generate beeps_30min.wav: 30 min, 48 kHz, mono, 16-bit PCM.
# A 20 ms, 1 kHz tone starts at every whole second (t = 0, 1, ..., 1799 s);
# everything else is digital silence.
#
# Usage (PowerShell):
#   powershell -NoProfile -ExecutionPolicy Bypass -File make_beeps.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File make_beeps.ps1 -FFmpeg "C:\path\to\ffmpeg.exe"
#
# The WAV is ignored by .gitignore (*.wav); regenerate it locally with this script.

param(
    [string]$FFmpeg = 'ffmpeg',
    [string]$OutFile = (Join-Path $PSScriptRoot 'beeps_30min.wav')
)

$ErrorActionPreference = 'Stop'

# aevalsrc: t is time in seconds. The expression is single-quoted inside the
# filter argument so that its commas are not read as filter-chain separators.
$expr = '0.8*sin(2*PI*1000*t)*lt(mod(t,1),0.02)'
$src = "aevalsrc='$expr':s=48000:d=1800:c=mono"

& $FFmpeg -hide_banner -loglevel error -y -f lavfi -i $src -ac 1 -ar 48000 -c:a pcm_s16le $OutFile
if ($LASTEXITCODE -ne 0) {
    throw "ffmpeg failed with exit code $LASTEXITCODE"
}
Write-Output "Wrote $OutFile"
