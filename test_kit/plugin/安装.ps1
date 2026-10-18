# 安装 x264guiEx-FreeRenderFPS.aux2 (精确模式版本) 到 AviUtl2 插件目录。
# 只替换 .aux2 本体; 不修改 .ini / .conf / exe_files。
# 运行方法: 关闭 AviUtl2 后, 右键本文件 > 使用 PowerShell 运行; 或在 PowerShell 中执行:
#   powershell -NoProfile -ExecutionPolicy Bypass -File ".\安装.ps1"
# 写入 C:\ProgramData 需要管理员权限; 非管理员运行时会自动请求提升 (UAC)。

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

$pluginDir = 'C:\ProgramData\aviutl2\Plugin\x264guiEx-FreeRenderFPS'
$fileName  = 'x264guiEx-FreeRenderFPS.aux2'
$target    = Join-Path $pluginDir $fileName
$backup    = "$target.before-precise"
$source    = Join-Path $PSScriptRoot $fileName

function Show-Hash([string]$path) {
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
}

try {
    Write-Host "=== 安装 $fileName (精确模式) ==="
    Write-Host "插件目录: $pluginDir"
    Write-Host "来源文件: $source"
    Write-Host ""

    # 步骤 1: 检查 AviUtl2 是否在运行 (运行中替换 .aux2 会失败或导致不一致)
    Write-Host "[1/4] 检查 AviUtl2 是否在运行 ..."
    $running = Get-Process -Name 'aviutl2' -ErrorAction SilentlyContinue
    if ($running) {
        Write-Host "      AviUtl2 正在运行 (aviutl2.exe)。请先关闭 AviUtl2, 然后重新运行本脚本。"
        Write-Host "      已退出, 未做任何修改。"
        Exit-Script 1
    }
    Write-Host "      未在运行。"

    # 步骤 2: 检查来源文件与目标插件是否存在
    Write-Host "[2/4] 检查文件 ..."
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        Write-Host "      错误: 找不到来源文件 $source"
        Write-Host "      已退出, 未做任何修改。"
        Exit-Script 1
    }
    if (-not (Test-Path -LiteralPath $target -PathType Leaf)) {
        Write-Host "      错误: 找不到已安装的插件 $target"
        Write-Host "      请先确认 AviUtl2 插件目录正确 (本脚本只替换已存在的 .aux2, 不新建)。已退出, 未做任何修改。"
        Exit-Script 1
    }
    $sourceHash = Show-Hash $source
    Write-Host "      来源文件 SHA-256: $sourceHash"
    Write-Host "      当前已安装 SHA-256: $(Show-Hash $target)"

    # 步骤 3: 备份原始 .aux2 (不覆盖已有备份)
    Write-Host "[3/4] 备份原始 .aux2 ..."
    if (Test-Path -LiteralPath $backup) {
        Write-Host "      提示: 备份已存在, 不会覆盖: $backup"
        Write-Host "      (如需重新备份, 请先自行把该备份移走。)"
    } else {
        Copy-Item -LiteralPath $target -Destination $backup
        Write-Host "      已备份到: $backup"
    }

    # 步骤 4: 复制新的 .aux2 到插件目录
    Write-Host "[4/4] 复制新的 .aux2 到插件目录 ..."
    try {
        Copy-Item -LiteralPath $source -Destination $target -Force -ErrorAction Stop
    } catch {
        Write-Host "      失败: 无法写入 $target"
        Write-Host "      原因: $($_.Exception.Message)"
        Write-Host "      请确认已以管理员身份运行, 且 AviUtl2 已完全关闭。插件文件可能未被完整替换。"
        Exit-Script 1
    }
    Write-Host "      已复制到: $target"
    $installedHash = Show-Hash $target
    Write-Host "      安装后 SHA-256: $installedHash"
    if ($installedHash -eq $sourceHash) {
        Write-Host "      安装成功，SHA-256 一致"
    } else {
        Write-Host "      错误: SHA-256 不一致, 安装未完成。请勿启动 AviUtl2, 可运行 回退.ps1 恢复。"
        Exit-Script 1
    }
    Write-Host ""
    Write-Host "完成。现在可以启动 D:\aviutl2 v2.1.12\aviutl2.exe (不要用指向 beta42 的快捷方式)。"
    Write-Host "回退: 运行 回退.ps1 (用 .before-precise 备份恢复)。"
} catch {
    # 未预期的错误: 只打印简短原因, 不显示长堆栈
    Write-Host ""
    Write-Host "错误: $($_.Exception.Message)"
    Write-Host "已中止。若已开始复制, 请检查插件目录与 .before-precise 备份。"
    Exit-Script 1
}

Pause-Close
