# 生成 net_test 的 WSS 自签证书, 导出为 build\test_wss.pfx 后从证书存储移除。
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$out = Join-Path $root 'build\test_wss.pfx'
$pass = ConvertTo-SecureString -String 'cui-test' -Force -AsPlainText

$cert = New-SelfSignedCertificate -DnsName 'localhost' -Subject 'CN=localhost-cui-test' `
    -CertStoreLocation Cert:\CurrentUser\My -KeyExportPolicy Exportable `
    -KeyUsage DigitalSignature -KeyAlgorithm RSA -KeyLength 2048 `
    -NotAfter (Get-Date).AddYears(1)
try {
    Export-PfxCertificate -Cert $cert -FilePath $out -Password $pass | Out-Null
    Write-Host ('cert written: ' + $out)
} finally {
    Remove-Item -Path ('Cert:\CurrentUser\My\' + $cert.Thumbprint) -Force -ErrorAction SilentlyContinue
}
