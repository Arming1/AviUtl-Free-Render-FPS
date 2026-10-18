﻿# 回退: 用 x264guiEx-FreeRenderFPS.aux2.before-precise 备份恢复插件目录中的 .aux2。
# 只恢复 .aux2 本体; 不修改 .ini / .conf / exe_files。
# 运行方法: 关闭 AviUtl2 后, 右键本文件 > 使用 PowerShell 运行; 或在 PowerShell 中执行:
#   powershell -NoProfile -ExecutionPolicy Bypass -File ".\回退.ps1"
# 插件目录位于 C:\ProgramData, 需要管理员权限; 非管理员时会自动请求提升 (UAC)。

$ErrorActionPreference = 'Stop'

# 结束窗口前统一提示, 避免窗口一闪而过
function Exit-Script {
    param([int]$Code)
    Write-Host ""
    Write-Host "按回车键关闭窗口"
    [void](Read-Host)
    exit $Code
}

# 把异常转换为中文原因, 不输出红色堆栈
function Get-FailureReason {
    param($ErrorRecord)
    $ex = $ErrorRecord.Exception
    if ($ex -is [System.UnauthorizedAccessException]) {
        return '权限不足 (访问被拒绝), 请确认是以管理员身份运行。'
    }
    if ($ex -is [System.IO.IOException]) {
        return '文件正被占用或读写失败, 请确认 AviUtl2 已完全关闭。'
    }
    return "$($ex.Message)"
}

# 步骤 0: 检测管理员权限; 非管理员则以管理员身份重新启动本脚本
$principal = New-Object System.Security.Principal.WindowsPrincipal([System.Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "需要管理员权限，正在请求提升……"
    $scriptPath = $PSCommandPath
    try {
        Start-Process -FilePath 'powershell.exe' -Verb RunAs -ErrorAction Stop `
            -ArgumentList ('-NoProfile -ExecutionPolicy Bypass -File "{0}"' -f $scriptPath)
    } catch {
        $inner = $_.Exception.InnerException
        if ($inner -is [System.ComponentModel.Win32Exception] -and $inner.NativeErrorCode -eq 1223) {
            Write-Host "已取消，未做任何修改。"
        } else {
            Write-Host "错误: 无法以管理员身份启动脚本。$(Get-FailureReason $_)"
            Write-Host "已退出，未做任何修改。"
        }
        Exit-Script 1
    }
    Write-Host "已在管理员窗口中继续运行，本窗口可以关闭。"
    Exit-Script 0
}

$pluginDir = 'C:\ProgramData\aviutl2\Plugin\x264guiEx-FreeRenderFPS'
$fileName  = 'x264guiEx-FreeRenderFPS.aux2'
$target    = Join-Path $pluginDir $fileName
$backup    = "$target.before-precise"

Write-Host "=== 回退 $fileName ==="
Write-Host "插件目录: $pluginDir"
Write-Host "备份文件: $backup"
Write-Host ""

# 步骤 1: 检查 AviUtl2 是否在运行
Write-Host "[1/3] 检查 AviUtl2 是否在运行 ..."
$running = Get-Process -Name 'aviutl2' -ErrorAction SilentlyContinue
if ($running) {
    Write-Host "      AviUtl2 正在运行 (aviutl2.exe)。请先关闭 AviUtl2, 然后重新运行本脚本。"
    Write-Host "      已退出, 未做任何修改。"
    Exit-Script 1
}
Write-Host "      未在运行。"

# 步骤 2: 检查备份是否存在
Write-Host "[2/3] 检查备份 ..."
if (-not (Test-Path -LiteralPath $backup -PathType Leaf)) {
    Write-Host "      错误: 找不到备份 $backup"
    Write-Host "      无法回退。已退出, 未做任何修改。"
    Exit-Script 1
}
try {
    $backupHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $backup -ErrorAction Stop).Hash
} catch {
    Write-Host "      错误: 无法读取备份文件。$(Get-FailureReason $_)"
    Write-Host "      无法回退。已退出, 未做任何修改。"
    Exit-Script 1
}
Write-Host "      备份 SHA-256: $backupHash"

# 步骤 3: 用备份覆盖插件目录中的 .aux2
Write-Host "[3/3] 从备份恢复 .aux2 ..."
try {
    Copy-Item -LiteralPath $backup -Destination $target -Force -ErrorAction Stop
} catch {
    Write-Host "      错误: 覆盖插件文件失败。$(Get-FailureReason $_)"
    Write-Host "      目标: $target"
    Write-Host "      未完成回退。"
    Exit-Script 1
}
Write-Host "      已恢复到: $target"
try {
    $restoredHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $target -ErrorAction Stop).Hash
} catch {
    Write-Host "      错误: 无法读取恢复后的文件。$(Get-FailureReason $_)"
    Write-Host "      恢复可能未完成, 请重新运行本脚本。"
    Exit-Script 1
}
Write-Host "      恢复后 SHA-256: $restoredHash"
if ($restoredHash -ne $backupHash) {
    Write-Host "      错误: 核对失败, 恢复后的文件与备份 SHA-256 不一致。"
    Exit-Script 1
}
Write-Host "      核对一致: 恢复后文件与备份 SHA-256 相同。"
Write-Host ""
Write-Host "完成。备份文件保留在原处, 未删除。"
Exit-Script 0
