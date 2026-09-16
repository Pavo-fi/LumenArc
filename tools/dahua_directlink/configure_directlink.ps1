<#
.SYNOPSIS
    大华录像机直连：给本机有线网卡配置与录像机同网段的静态 IP。

.DESCRIPTION
    自动挑选"插了网线、且不是无线"的物理网卡，默认配置 192.168.1.100/24，
    然后验证录像机的 RTSP 554 端口是否可达。原配置会存档，
    可用 restore_dhcp.ps1 一键还原。

    为什么需要它：网线直连时链路里没有 DHCP 服务器，Windows 只会给网卡一个
    169.254.x.x 的自动地址，任何 RTSP 都连不上。

.PARAMETER NvrIp
    录像机地址，取其 /24 网段作为本机静态 IP 的网段。默认 192.168.1.108。

.PARAMETER HostOctet
    本机 IP 的最后一段。默认 100。

.PARAMETER AdapterName
    强制指定网卡（Get-NetAdapter 的 Name）。多张有线网卡都插着时用。

.PARAMETER DryRun
    只打印将要做什么，不做任何修改（不需要管理员权限）。

.PARAMETER Force
    跳过确认提示（用于脚本编排）。

.EXAMPLE
    .\configure_directlink.ps1
    .\configure_directlink.ps1 -DryRun
    .\configure_directlink.ps1 -NvrIp 192.168.1.108 -HostOctet 101
#>
[CmdletBinding()]
param(
    [string]$NvrIp = '192.168.1.108',
    [int]$HostOctet = 100,
    [string]$AdapterName,
    [switch]$DryRun,
    [switch]$Force
)

# 控制台输出统一为 UTF-8（配合 .bat 的 chcp 65001），避免中文乱码
# 注：不设此项时 Write-Host 会按控制台 OEM 代码页（中文系统为 936）输出，
# 在管道/重定向场景会被误读为乱码。
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

$ErrorActionPreference = 'Stop'

$StateDir  = Join-Path $env:LOCALAPPDATA 'LumenArc'
$StatePath = Join-Path $StateDir 'directlink_state.json'

# ---------------------------------------------------------------- helpers
function Write-Head($t) { Write-Host ''; Write-Host "== $t ==" -ForegroundColor Cyan }
function Write-Ok($t)   { Write-Host "  [OK]   $t" -ForegroundColor Green }
function Write-Note($t) { Write-Host "  [--]   $t" }
function Write-Warn2($t){ Write-Host "  [WARN] $t" -ForegroundColor Yellow }
function Write-Err2($t) { Write-Host "  [FAIL] $t" -ForegroundColor Red }

function Test-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    (New-Object Security.Principal.WindowsPrincipal($id)).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)
}

# TCP 端口连通性（比 ping 更贴近实际需求：LumenArc 要的是 554 能连）
function Test-TcpPort([string]$Target, [int]$Port, [int]$TimeoutMs = 2000) {
    $client = New-Object System.Net.Sockets.TcpClient
    try {
        $iar = $client.BeginConnect($Target, $Port, $null, $null)
        if ($iar.AsyncWaitHandle.WaitOne($TimeoutMs, $false)) {
            $client.EndConnect($iar)
            return $true
        }
        return $false
    } catch {
        return $false
    } finally {
        $client.Close()
    }
}

# ---------------------------------------------------------------- 0. 前置
Write-Host '大华录像机直连 — 本机网卡配置' -ForegroundColor White
Write-Note "录像机地址 : $NvrIp"
if ($DryRun) { Write-Warn2 'DryRun 模式：只检查，不修改任何配置。' }

if (-not $DryRun -and -not (Test-Admin)) {
    Write-Err2 '需要管理员权限才能修改 IP 配置。'
    Write-Host '  请右键"以管理员身份运行"配套的 .bat，或执行：'
    Write-Host "    Start-Process powershell -Verb RunAs -ArgumentList '-File `"$PSCommandPath`"'"
    exit 1
}

# ---------------------------------------------------------------- 1. 校验入参
if ($NvrIp -notmatch '^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$') {
    Write-Err2 "录像机 IP 格式不正确：$NvrIp"; exit 1
}
$o = @([int]$matches[1], [int]$matches[2], [int]$matches[3], [int]$matches[4])
if ($o | Where-Object { $_ -lt 0 -or $_ -gt 255 }) {
    Write-Err2 "录像机 IP 超出范围：$NvrIp"; exit 1
}
if ($HostOctet -lt 1 -or $HostOctet -gt 254) {
    Write-Err2 "HostOctet 必须在 1-254 之间：$HostOctet"; exit 1
}
$subnet  = "$($o[0]).$($o[1]).$($o[2])"
$localIp = "$subnet.$HostOctet"
if ($localIp -eq $NvrIp) {
    Write-Err2 "本机 IP 与录像机 IP 相同（$NvrIp），请换一个 -HostOctet"; exit 1
}
Write-Note "本机静态 IP: $localIp / 255.255.255.0"

# ---------------------------------------------------------------- 2. 选网卡
$wired = @(Get-NetAdapter -Physical | Where-Object {
    $_.InterfaceDescription -notmatch 'Wi-?Fi|Wireless|WLAN|802\.11'
})

if ($AdapterName) {
    $sel = @($wired | Where-Object { $_.Name -eq $AdapterName })
    if ($sel.Count -eq 0) { Write-Err2 "找不到指定网卡：$AdapterName"; exit 1 }
} else {
    $up = @($wired | Where-Object { $_.Status -eq 'Up' })
    if ($up.Count -eq 0) {
        Write-Head '没有可用的有线网卡'
        $down = @($wired | Where-Object { $_.Status -eq 'Disconnected' })
        if ($down.Count -gt 0) {
            Write-Warn2 '检测到有线网卡，但未插网线：'
            $down | ForEach-Object { Write-Host "         - $($_.Name)  ($($_.InterfaceDescription))" }
            Write-Host ''
            Write-Host '  请把网线一头插录像机、一头插本机网口，然后重新运行。' -ForegroundColor Yellow
        } else {
            Write-Warn2 '本机没有有线网卡（可用 USB 转网口适配器，或检查驱动）。'
        }
        exit 2
    }
    if ($up.Count -eq 1) {
        $sel = $up
    } else {
        # 直连场景的网卡通常没有默认网关，优先选它
        $noGw = @($up | Where-Object {
            -not (Get-NetRoute -InterfaceIndex $_.ifIndex -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue)
        })
        if ($noGw.Count -eq 1) {
            $sel = $noGw
        } else {
            Write-Head '检测到多张已连接的有线网卡，请选择'
            for ($i = 0; $i -lt $up.Count; $i++) {
                Write-Host ("    [{0}] {1}  ({2})  {3}" -f ($i + 1), $up[$i].Name,
                            $up[$i].InterfaceDescription, $up[$i].LinkSpeed)
            }
            $pick = Read-Host '  输入编号'
            $idx = 0
            if (-not [int]::TryParse($pick, [ref]$idx) -or $idx -lt 1 -or $idx -gt $up.Count) {
                Write-Err2 '选择无效。'; exit 1
            }
            $sel = @($up[$idx - 1])
        }
    }
}
$nic = $sel[0]

# ---------------------------------------------------------------- 3. 现状快照
$ipIf   = Get-NetIPInterface -InterfaceIndex $nic.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue
$addrs  = @(Get-NetIPAddress -InterfaceIndex $nic.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue |
            Where-Object { $_.IPAddress -ne '127.0.0.1' })
$gw     = @(Get-NetRoute -InterfaceIndex $nic.ifIndex -DestinationPrefix '0.0.0.0/0' -ErrorAction SilentlyContinue)
$wasDhcp = [bool]($ipIf -and $ipIf.Dhcp -eq 'Enabled')

Write-Head '目标网卡'
Write-Note "名称   : $($nic.Name)"
Write-Note "型号   : $($nic.InterfaceDescription)"
Write-Note "链路   : $($nic.Status)  $($nic.LinkSpeed)"
Write-Note "获取方式: $(if ($wasDhcp) { 'DHCP（自动）' } else { '静态（手动）' })"
if ($addrs) { Write-Note "当前 IPv4: $((@($addrs | Select-Object -ExpandProperty IPAddress)) -join ', ')" }
if ($gw) {
    Write-Warn2 "该网卡当前有默认网关（$($gw[0].NextHop)）——改为静态且不带网关后，它上面的上网可能会中断。"
}

# ---------------------------------------------------------------- 4. 确认
if (-not $DryRun -and -not $Force) {
    if ($gw) {
        $a = Read-Host '  仍要继续吗？(y/N)'
        if ($a -notmatch '^(y|Y)') { Write-Host '  已取消。'; exit 1 }
    }
    $a = Read-Host "  将把 '$($nic.Name)' 设为 $localIp/24，确认？(y/N)"
    if ($a -notmatch '^(y|Y)') { Write-Host '  已取消。'; exit 1 }
}

# ---------------------------------------------------------------- 5. 存档原配置
if (-not $DryRun) {
    if (-not (Test-Path $StateDir)) { New-Item -ItemType Directory -Path $StateDir -Force | Out-Null }
    $state = [ordered]@{
        adapterName     = $nic.Name
        interfaceIndex  = $nic.ifIndex
        wasDhcp         = $wasDhcp
        previousIp      = @($addrs | Select-Object -ExpandProperty IPAddress)
        previousPrefix  = @($addrs | Select-Object -ExpandProperty PrefixLength)
        previousGateway = @($gw    | Select-Object -ExpandProperty NextHop)
        nvrIp           = $NvrIp
        configuredIp    = $localIp
        savedAt         = (Get-Date).ToString('s')
    }
    ($state | ConvertTo-Json -Depth 4) | Set-Content -Path $StatePath -Encoding UTF8
    Write-Ok "原配置已存档：$StatePath"
}

# ---------------------------------------------------------------- 6. 应用
if ($DryRun) {
    Write-Head 'DryRun：将执行'
    Write-Host "  netsh interface ipv4 set address name=`"$($nic.Name)`" static $localIp 255.255.255.0"
} else {
    Write-Head '正在应用'
    $out = & netsh interface ipv4 set address name="$($nic.Name)" static $localIp 255.255.255.0 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Err2 "netsh 执行失败（退出码 $LASTEXITCODE）：$out"
        exit 1
    }
    Write-Ok "已设为静态 $localIp / 255.255.255.0"
    Start-Sleep -Milliseconds 800
}

# ---------------------------------------------------------------- 7. 验证
Write-Head '验证录像机可达性'
$tcpOk = Test-TcpPort $NvrIp 554
if ($tcpOk) { Write-Ok "RTSP 554 可达（$NvrIp）——LumenArc 可以连接" }
else       { Write-Warn2 "RTSP 554 暂时不可达（$NvrIp）" }

$pingOk = $false
try { $pingOk = Test-Connection -ComputerName $NvrIp -Count 2 -Quiet -ErrorAction SilentlyContinue } catch { $pingOk = $false }
if ($pingOk) { Write-Ok "ping $NvrIp 通" }
else         { Write-Warn2 "ping $NvrIp 不通（部分录像机屏蔽 ICMP，不代表不可用）" }

# ---------------------------------------------------------------- 8. 收尾
Write-Head '下一步'
if (-not $tcpOk) {
    Write-Host '  暂不可达，请依次确认：'
    Write-Host '    1) 网线两端是否插紧（本机网口指示灯）'
    Write-Host "    2) 录像机 IP 是否就是 $NvrIp"
    Write-Host '    3) 录像机「网络设置 → RTSP」是否开启'
    Write-Host '    4) 账号是否具备远程预览权限'
    Write-Host ''
}
Write-Host '  打开 LumenArc：菜单「文件 → 接入监控直播」，填：'
Write-Host "    地址   : $NvrIp"
Write-Host '    端口   : 554'
Write-Host '    用户   : admin（你的设备账号）'
Write-Host '    通道   : 1     码流: 主码流     传输: TCP'
Write-Host ''
Write-Host '  想还原本次改动：运行 restore_dhcp.bat'
Write-Host '  说明：Windows 可能在该网卡上显示"无 Internet 访问"——直连场景属正常。'
exit 0
