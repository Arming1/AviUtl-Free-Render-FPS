# 一键分析
#   a. 8 组精确模式: 对 <8组运行目录> 中的 <case>.mp4 依次运行 analyze.py 的
#      --timeline precise / original / retimed, 三份 SUMMARY 分别另存。
#   b. 长视频音画同步: 对 <长视频目录> 中存在的 long_66.mp4 / long_50.mp4 /
#      long_5994.mp4 运行 analyze_sync.py, 结果写入 长视频_<名称>.txt。
#   所有输出都写在本脚本所在的文件夹 (test_kit\分析\) 下, 用户只需把该文件夹打包发回。
#
# 运行方法: 右键本文件 > 使用 PowerShell 运行; 或在 PowerShell 中执行:
#   powershell -NoProfile -ExecutionPolicy Bypass -File ".\一键分析.ps1"
#
# 可选参数 (一般不需要): -Run8Dir / -LongDir 指定输入目录; -OutDir 指定输出目录;
#   -FFmpeg / -FFprobe / -Python 指定可执行文件 (默认: 先找 PATH, 找不到则用
#   D:\FFmpeg-master\ffmpeg-2025-11-27-git-61b034a47c-full_build\bin 下的 ffmpeg.exe / ffprobe.exe)。

param(
    [string]$Run8Dir,
    [string]$LongDir,
    [string]$OutDir,
    [string]$FFmpeg,
    [string]$FFprobe,
    [string]$Python = 'python'
)

$ErrorActionPreference = 'Stop'
$env:PYTHONIOENCODING = 'utf-8'

# 路径全部相对 $PSScriptRoot (= test_kit\分析) 计算
$kitDir     = Split-Path -Parent $PSScriptRoot
$repo       = Split-Path -Parent $kitDir
$matrixPy   = Join-Path $repo 'experiments\free_fps_matrix\analyze.py'
$casesJson  = Join-Path $repo 'experiments\free_fps_matrix\cases.json'
$syncPy     = Join-Path $repo 'experiments\long_av_sync\analyze_sync.py'

if (-not $Run8Dir) { $Run8Dir = Join-Path $kitDir '1_精确模式8组\out' }
if (-not $LongDir) { $LongDir = Join-Path $kitDir '2_长视频音画同步\out' }
if (-not $OutDir)  { $OutDir  = $PSScriptRoot }

# 长视频: 文件名与目标帧率 (与 test_kit\使用说明.zh-CN.md 第三节对齐)
$longCases = @(
    @{ Name = '66';   File = 'long_66.mp4';   Fps = '66' },
    @{ Name = '50';   File = 'long_50.mp4';   Fps = '50' },
    @{ Name = '5994'; File = 'long_5994.mp4'; Fps = '60000/1001' }
)

$rows = New-Object System.Collections.Generic.List[object]

function Add-Row([string]$item, [string]$status, [string]$detail) {
    $script:rows.Add([pscustomobject]@{ 项目 = $item; 状态 = $status; 详情 = $detail })
}

# 运行外部程序, stdout / stderr 分别重定向到文件, 返回退出码
function Invoke-Native([string]$exe, [string[]]$arguments, [string]$stdoutPath, [string]$stderrPath) {
    $quoted = @($arguments | ForEach-Object { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } })
    $p = Start-Process -FilePath $exe -ArgumentList $quoted -NoNewWindow -Wait -PassThru `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    return $p.ExitCode
}

function Remove-IfExists([string]$path) {
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Recurse -Force }
}

Write-Host "=== 一键分析 ==="
Write-Host "输出目录: $OutDir"
Write-Host ""

# 查找可执行文件: 指定了参数则只用参数; 未指定则先找 PATH, 再回退到固定的 ffmpeg 目录
$fallbackBin = 'D:\FFmpeg-master\ffmpeg-2025-11-27-git-61b034a47c-full_build\bin'
function Resolve-Exe([string]$given, [string]$name, [string]$fallbackPath) {
    if ($given) {
        if (Test-Path -LiteralPath $given -PathType Leaf) { return (Resolve-Path -LiteralPath $given).Path }
        $c = Get-Command $given -ErrorAction SilentlyContinue
        if ($c) { return $c.Source }
        return $null
    }
    $c = Get-Command $name -ErrorAction SilentlyContinue
    if ($c) { return $c.Source }
    if (Test-Path -LiteralPath $fallbackPath -PathType Leaf) { return $fallbackPath }
    return $null
}

$ffmpegPath  = Resolve-Exe $FFmpeg  'ffmpeg'  (Join-Path $fallbackBin 'ffmpeg.exe')
$ffprobePath = Resolve-Exe $FFprobe 'ffprobe' (Join-Path $fallbackBin 'ffprobe.exe')
$pyCmd       = Get-Command $Python -ErrorAction SilentlyContinue

if (-not $pyCmd) {
    Write-Host "错误: 找不到 Python ($Python)。请安装 Python 3 (含 numpy) 并加入 PATH, 或用 -Python 指定。"
    exit 2
}
if (-not $ffmpegPath) {
    Write-Host "错误: 找不到 ffmpeg。PATH 中没有, 固定位置也不存在: $fallbackBin\ffmpeg.exe"
    Write-Host "请安装 ffmpeg 并加入 PATH, 或用 -FFmpeg 指定完整路径。已退出, 未做任何修改。"
    exit 2
}
if (-not $ffprobePath) {
    Write-Host "错误: 找不到 ffprobe。PATH 中没有, 固定位置也不存在: $fallbackBin\ffprobe.exe"
    Write-Host "请安装 ffprobe (通常与 ffmpeg 同包) 并加入 PATH, 或用 -FFprobe 指定完整路径。已退出, 未做任何修改。"
    exit 2
}
$FFmpeg  = $ffmpegPath
$FFprobe = $ffprobePath
$Python  = $pyCmd.Source
Write-Host "使用的 python : $Python"
Write-Host "使用的 ffmpeg : $FFmpeg"
Write-Host "使用的 ffprobe: $FFprobe"
Write-Host ""
if (-not (Test-Path -LiteralPath $OutDir)) { New-Item -ItemType Directory -Force -Path $OutDir | Out-Null }

# ---------- a. 8 组精确模式 ----------
Write-Host "=== a. 8 组精确模式 ==="
Write-Host "运行目录: $Run8Dir"
$ids = @((Get-Content -LiteralPath $casesJson -Encoding UTF8 -Raw | ConvertFrom-Json).cases | ForEach-Object { $_.id })
$present = @($ids | Where-Object { Test-Path -LiteralPath (Join-Path $Run8Dir "$_.mp4") -PathType Leaf })
$missing = @($ids | Where-Object { $present -notcontains $_ })

if ($present.Count -eq 0) {
    Write-Host "跳过: 运行目录中没有任何 <case>.mp4 (目录: $Run8Dir)"
    Add-Row '8组精确模式' '跳过' '没有任何 mp4'
} else {
    if ($missing.Count -gt 0) {
        Write-Host "提示: 缺失 $($missing.Count) 个视频, 已跳过: $($missing -join ', ')"
        Add-Row '8组精确模式' '部分缺失' ("缺少: " + ($missing -join ', '))
    }
    $only = $present -join ','
    foreach ($mode in 'precise', 'original', 'retimed') {
        Write-Host ""
        Write-Host "--- 时间轴: $mode ---"
        # analyze.py 每次都会覆盖运行目录下的 SUMMARY.md / summary.json / analysis\, 先清掉旧结果
        Remove-IfExists (Join-Path $Run8Dir 'SUMMARY.md')
        Remove-IfExists (Join-Path $Run8Dir 'summary.json')
        Remove-IfExists (Join-Path $Run8Dir 'analysis')

        $pyArgs = @('-I', $matrixPy, $Run8Dir, '--timeline', $mode, '--only', $only,
                    '--ffmpeg', $FFmpeg, '--ffprobe', $FFprobe)
        $stdout = Join-Path $OutDir "8组_$mode.log"
        $stderr = Join-Path $OutDir "8组_$mode.stderr.tmp"
        $code = Invoke-Native $Python $pyArgs $stdout $stderr
        if ((Test-Path -LiteralPath $stderr) -and (Get-Item -LiteralPath $stderr).Length -gt 0) {
            Add-Content -LiteralPath $stdout -Value '--- stderr ---' -Encoding UTF8
            Get-Content -LiteralPath $stderr -Encoding UTF8 | Add-Content -LiteralPath $stdout -Encoding UTF8
        }
        Remove-IfExists $stderr

        $summaryMd   = Join-Path $Run8Dir 'SUMMARY.md'
        $summaryJson = Join-Path $Run8Dir 'summary.json'
        if (-not (Test-Path -LiteralPath $summaryJson -PathType Leaf)) {
            Write-Host "错误: analyze.py 退出码 $code, 没有生成 summary.json。详见 $stdout"
            Add-Row "8组 $mode" '出错' "退出码 $code, 详见 8组_$mode.log"
            continue
        }
        # 另存, 避免下一个时间轴覆盖
        Copy-Item -LiteralPath $summaryMd -Destination (Join-Path $OutDir "SUMMARY_$mode.md") -Force
        Copy-Item -LiteralPath $summaryJson -Destination (Join-Path $OutDir "summary_$mode.json") -Force
        $analysisDst = Join-Path $OutDir "8组_analysis_$mode"
        Remove-IfExists $analysisDst
        Copy-Item -LiteralPath (Join-Path $Run8Dir 'analysis') -Destination $analysisDst -Recurse -Force

        $overall = (Get-Content -LiteralPath $summaryJson -Encoding UTF8 -Raw | ConvertFrom-Json).overall
        Write-Host "结果: overall $overall (退出码 $code) -> SUMMARY_$mode.md"
        Add-Row "8组 $mode" "$overall" "退出码 $code, 已另存 SUMMARY_$mode.md"
    }
}

# ---------- b. 长视频音画同步 ----------
Write-Host ""
Write-Host "=== b. 长视频音画同步 ==="
Write-Host "输入目录: $LongDir"
foreach ($c in $longCases) {
    $mp4 = Join-Path $LongDir $c.File
    $item = "长视频 $($c.Name)"
    if (-not (Test-Path -LiteralPath $mp4 -PathType Leaf)) {
        Write-Host "跳过: 缺少 $($c.File)"
        Add-Row $item '跳过' "缺少 $($c.File)"
        continue
    }
    $txt = Join-Path $OutDir "长视频_$($c.Name).txt"
    $stderr = Join-Path $OutDir "长视频_$($c.Name).stderr.tmp"
    $pyArgs = @('-I', $syncPy, $mp4, '--target-fps', $c.Fps, '--ffmpeg', $FFmpeg, '--ffprobe', $FFprobe)
    Write-Host "分析: $($c.File) (目标帧率 $($c.Fps))"
    $code = Invoke-Native $Python $pyArgs $txt $stderr
    if ((Test-Path -LiteralPath $stderr) -and (Get-Item -LiteralPath $stderr).Length -gt 0) {
        Add-Content -LiteralPath $txt -Value '--- stderr ---' -Encoding UTF8
        Get-Content -LiteralPath $stderr -Encoding UTF8 | Add-Content -LiteralPath $txt -Encoding UTF8
    }
    Remove-IfExists $stderr

    $lines = @(Get-Content -LiteralPath $txt -Encoding UTF8)
    $verdict = ($lines | Where-Object { $_ -match '^总判定' } | Select-Object -First 1)
    $mean = ($lines | Where-Object { $_ -match '^\s+均值' } | Select-Object -First 1)
    $verdictText = if ($verdict -match '(PASS|FAIL)') { $Matches[1] } else { "未知(退出码 $code)" }
    $meanText = if ($mean -match '均值\s+(\S+)') { $Matches[1] + ' ms' } else { '-' }
    Write-Host "结果: $verdictText, 均值偏移 $meanText -> 长视频_$($c.Name).txt"
    Add-Row $item $verdictText "均值偏移 $meanText, 退出码 $code"
}

# ---------- 汇总 ----------
Write-Host ""
Write-Host "=== 汇总 ==="
$rows | Format-Table -AutoSize | Out-String -Width 300 | Write-Host
Write-Host "所有输出在: $OutDir"
Write-Host "请把 test_kit\分析\ 整个文件夹打包发回。"
