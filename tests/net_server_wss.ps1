# 本机 WebSocket over TLS 测试服务器 (18082, 自签证书)
$ErrorActionPreference = 'Continue'
$log = Join-Path $PSScriptRoot 'wss.log'
function Log($s) { Add-Content -Path $log -Value ((Get-Date).ToString('HH:mm:ss.fff') + ' ' + $s) }

function Read-Exact($stream, $n) {
    $b = New-Object byte[] $n
    $o = 0
    while ($o -lt $n) {
        $r = $stream.Read($b, $o, $n - $o)
        if ($r -le 0) { throw 'eof' }
        $o += $r
    }
    return ,$b
}

function Send-Frame($stream, $op, [byte[]]$payload) {
    $len = $payload.Length
    if ($len -lt 126) {
        $fh = New-Object byte[] 2
        $fh[0] = [byte](0x80 -bor $op)
        $fh[1] = [byte]$len
    } elseif ($len -lt 65536) {
        $fh = New-Object byte[] 4
        $fh[0] = [byte](0x80 -bor $op); $fh[1] = 126
        $fh[2] = [byte](($len -shr 8) -band 0xFF); $fh[3] = [byte]($len -band 0xFF)
    } else {
        $fh = New-Object byte[] 10
        $fh[0] = [byte](0x80 -bor $op); $fh[1] = 127
        $v = [uint64]$len
        for ($i = 0; $i -lt 8; $i++) { $fh[2 + $i] = [byte](($v -shr (56 - $i * 8)) -band 0xFF) }
    }
    $stream.Write($fh, 0, $fh.Length)
    if ($len -gt 0) { $stream.Write($payload, 0, $len) }
    $stream.Flush()
}

$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq 'CN=localhost-cui-test' } | Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -DnsName 'localhost' -Subject 'CN=localhost-cui-test' -CertStoreLocation Cert:\CurrentUser\My -KeyUsage DigitalSignature -KeyAlgorithm RSA -KeyLength 2048 -NotAfter (Get-Date).AddYears(1)
    Log ('created cert ' + $cert.Thumbprint)
}
Log ('using cert ' + $cert.Thumbprint)

$listener = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 18082)
$listener.Start()
Log 'WSS listen 18082'

while ($true) {
    $client = $listener.AcceptTcpClient()
    try {
        $st = $client.GetStream()
        $ssl = New-Object System.Net.Security.SslStream($st, $false)
        $ssl.AuthenticateAsServer($cert, $false, [System.Security.Authentication.SslProtocols]::Tls12, $false)
        Log 'TLS ok'
        $ssl.ReadTimeout = 60000

        $buf = New-Object byte[] 4096
        $ms = New-Object System.IO.MemoryStream
        while ($true) {
            $n = $ssl.Read($buf, 0, $buf.Length)
            if ($n -le 0) { throw 'eof' }
            $ms.Write($buf, 0, $n)
            $t = [Text.Encoding]::ASCII.GetString($ms.ToArray())
            if ($t.Contains("`r`n`r`n")) { break }
        }
        $t = [Text.Encoding]::ASCII.GetString($ms.ToArray())
        $key = ''
        foreach ($ln in ($t -split "`r`n")) {
            if ($ln -match '^(?i)Sec-WebSocket-Key:\s*(.+)$') { $key = $matches[1].Trim() }
        }
        $sha = [System.Security.Cryptography.SHA1]::Create()
        $raw = [Text.Encoding]::ASCII.GetBytes($key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11')
        $accept = [Convert]::ToBase64String($sha.ComputeHash($raw))
        $resp = "HTTP/1.1 101 Switching Protocols`r`nUpgrade: websocket`r`nConnection: Upgrade`r`nSec-WebSocket-Accept: $accept`r`n`r`n"
        $rb = [Text.Encoding]::ASCII.GetBytes($resp)
        $ssl.Write($rb, 0, $rb.Length); $ssl.Flush()
        Log 'WSS upgraded'

        while ($true) {
            $h = Read-Exact $ssl 2
            $op = $h[0] -band 0x0F
            $len = $h[1] -band 0x7F
            $masked = ($h[1] -band 0x80) -ne 0
            if ($len -eq 126) { $e = Read-Exact $ssl 2; $len = ($e[0] -shl 8) -bor $e[1] }
            elseif ($len -eq 127) {
                $e = Read-Exact $ssl 8; $len = 0
                foreach ($x in $e) { $len = $len * 256 + $x }
            }
            $mask = $null
            if ($masked) { $mask = Read-Exact $ssl 4 }
            if ($len -gt 0) { $p = Read-Exact $ssl $len } else { $p = New-Object byte[] 0 }
            if ($masked) { for ($i = 0; $i -lt $len; $i++) { $p[$i] = $p[$i] -bxor $mask[$i % 4] } }
            if ($op -eq 8) { Log 'WSS close'; Send-Frame $ssl 8 $p; break }
            if ($op -eq 9) { Send-Frame $ssl 10 $p; continue }
            if ($op -eq 10) { continue }
            if ($op -eq 1 -or $op -eq 2) {
                $text = [Text.Encoding]::UTF8.GetString($p)
                Log ('WSS recv len=' + $p.Length + ' ' + $text)
                $echo = [Text.Encoding]::UTF8.GetBytes('wss-echo:' + $text)
                Send-Frame $ssl 1 $echo
            }
        }
        $ssl.Close()
    } catch {
        Log ('WSS error: ' + $_.Exception.Message)
    } finally {
        $client.Close()
    }
}
