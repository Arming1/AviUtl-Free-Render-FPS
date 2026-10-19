# 卸载 SnapshotSaveProbe.aux2 (快照保存探针): 只删除 C:\ProgramData\aviutl2\Plugin\SnapshotSaveProbe.aux2 。
# 运行方法: 关闭 AviUtl2 后, 右键本文件 > 使用 PowerShell 运行; 或在 PowerShell 中执行:
#   powershell -NoProfile -ExecutionPolicy Bypass -File ".\卸载.ps1"
# 删除 C:\ProgramData 下的文件需要管理员权限; 非管理员运行时会自动请求提升 (UAC)。

$ErrorActionPreference = 'Stop'

# 窗口结束前停住, 避免双击运行时窗口一闪而过
function Pause-Close {
    Read-Host '按回车键关闭窗口' | Out-Null
}

# 所有提前退出都经过这里: 先停住, 再退出
function Exit-Script([int]$code) {
    Pause-Close
    exit $code
}

# 管理员检查: 不是管理员则以管理员身份重新启动自身, 当前进程随即退出
$identity  = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = New-Object Security.Principal.WindowsPrincipal($identity)
$isAdmin   = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "需要管理员权限，正在请求提升……"
    try {
        Start-Process -FilePath 'powershell.exe' -Verb RunAs -ErrorAction Stop `
            -ArgumentList ('-NoProfile -ExecutionPolicy Bypass -File "{0}"' -f $PSCommandPath)
    } catch {
        Write-Host "已取消，未做任何修改。"
        Exit-Script 1
    }
    exit 0
}

$pluginDir = 'C:\ProgramData\aviutl2\Plugin'
$fileName  = 'SnapshotSaveProbe.aux2'
$target    = Join-Path $pluginDir $fileName

try {
    Write-Host "=== 卸载 $fileName (快照保存探针) ==="
    Write-Host "目标文件: $target"
    Write-Host ""

    # 步骤 1: 检查 AviUtl2 是否在运行 (运行中不要删插件文件)
    Write-Host "[1/2] 检查 AviUtl2 是否在运行 ..."
    $running = Get-Process -Name 'aviutl2' -ErrorAction SilentlyContinue
    if ($running) {
        Write-Host "      AviUtl2 正在运行 (aviutl2.exe)。请先关闭 AviUtl2, 然后重新运行本脚本。"
        Write-Host "      已退出, 未做任何修改。"
        Exit-Script 1
    }
    Write-Host "      未在运行。"

    # 步骤 2: 删除
    Write-Host "[2/2] 删除 ..."
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
        Write-Host "      文件不存在, 无需删除: $target"
        Write-Host ""
        Write-Host "完成。"
        Exit-Script 0
    }
    try {
        Remove-Item -LiteralPath $target -Force -ErrorAction Stop
    } catch {
        Write-Host "      失败: 无法删除 $target"
        Write-Host "      原因: $($_.Exception.Message)"
        Write-Host "      请确认已以管理员身份运行, 且 AviUtl2 已完全关闭。"
        Exit-Script 1
    }
    if (Test-Path -LiteralPath $target) {
        Write-Host "      错误: 文件仍然存在, 请检查权限后重试。"
        Exit-Script 1
    }
    Write-Host "      已删除: $target"
    Write-Host ""
    Write-Host "完成。"
} catch {
    # 未预期的错误: 只打印简短原因, 不显示长堆栈
    Write-Host ""
    Write-Host "错误: $($_.Exception.Message)"
    Write-Host "已中止。"
    Exit-Script 1
}

Pause-Close
