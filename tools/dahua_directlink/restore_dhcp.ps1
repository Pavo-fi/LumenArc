<#
.SYNOPSIS
    还原 configure_directlink.ps1 对网卡所做的改动。

.DESCRIPTION
    读取 %LOCALAPPDATA%\LumenArc\directlink_state.json 中存档的原配置：
      - 原来用 DHCP 的 → 还原为"自动获取 IP"
      - 原来是静态的   → 还原为原来的 IP/掩码与默认网关
    还原后删除存档文件。

.PARAMETER Force
    跳过确认提示（用于脚本编排）。

.EXAMPLE
    .\restore_dhcp.ps1
#>
[CmdletBinding()]
param(
    [switch]$Force
)

# 控制台输出统一为 UTF-8（配合 .bat 的 chcp 65001），避免中文乱码
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

$ErrorActionPreference = 'Stop'

$StateDir  = Join-Path $env:LOCALAPPDATA 'LumenArc'
$StatePath = Join-Path $StateDir 'directlink_state.json'

function Write-Head($t) { Write-Host ''; Write-Host "== $t ==" -ForegroundColor Cyan }
function Write-Ok($t)   { Write-Host "  [OK]   $t" -ForegroundColor Green }
function Write-Note($t) { Write-Host "  [--]   $t" }
function Write-Err2($t) { Write-Host "  [FAIL] $t" -ForegroundColor Red }

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

Write-Host '大华录像机直连 — 还原网卡配置' -ForegroundColor White

if (-not (Test-Admin)) {
    Write-Err2 '需要管理员权限才能修改 IP 配置。请用配套的 restore_dhcp.bat（会自动提权）。'
    exit 1
}

if (-not (Test-Path $StatePath)) {
    Write-Err2 "找不到存档文件：$StatePath"
    Write-Note '说明：没有可还原的改动（可能从未配置过，或已经还原过）。'
    exit 2
}

$s = Get-Content -Path $StatePath -Raw | ConvertFrom-Json

Write-Head '存档内容'
Write-Note "网卡      : $($s.adapterName)"
Write-Note "原获取方式: $(if ($s.wasDhcp) { 'DHCP（自动）' } else { '静态（手动）' })"
Write-Note "原 IPv4   : $(@($s.previousIp) -join ', ')"
Write-Note "配置时间  : $($s.savedAt)"

if (-not $Force) {
    $a = Read-Host '  确认还原？(y/N)'
    if ($a -notmatch '^(y|Y)') { Write-Host '  已取消。'; exit 1 }
}

Write-Head '正在还原'
$name = $s.adapterName

if ($s.wasDhcp) {
    $out = & netsh interface ipv4 set address name="$name" source=dhcp 2>&1
    if ($LASTEXITCODE -ne 0) { Write-Err2 "还原 DHCP 失败：$out"; exit 1 }
    & netsh interface ipv4 set dns name="$name" source=dhcp 2>&1 | Out-Null
    Write-Ok '已还原为"自动获取 IP（DHCP）"'
} else {
    $ip = @($s.previousIp)[0]
    $pfx = @($s.previousPrefix)[0]
    if ($ip -and $pfx) {
        $out = & netsh interface ipv4 set address name="$name" static "$ip/$pfx" 2>&1
        if ($LASTEXITCODE -ne 0) { Write-Err2 "还原静态 IP 失败：$out"; exit 1 }
        Write-Ok "已还原静态 IP：$ip/$pfx"
    } else {
        & netsh interface ipv4 set address name="$name" source=dhcp 2>&1 | Out-Null
        Write-Ok '原配置无有效静态地址，已退回 DHCP'
    }
    foreach ($g in @($s.previousGateway)) {
        if ($g) {
            & netsh interface ipv4 set route 0.0.0.0/0 "$name" $g 2>&1 | Out-Null
            Write-Ok "已还原默认网关：$g"
        }
    }
}

Remove-Item -Path $StatePath -Force
Write-Ok '存档已清除'

Write-Head '完成'
Write-Note '可用 ipconfig 复核该网卡当前地址。'
exit 0
