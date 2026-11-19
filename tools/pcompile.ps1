# pcompile.ps1 - nmake 并行编译前置步骤
#
# NMAKE 没有 -j; 这里按 build.mk 的 CFLAGS/CXXFLAGS 找出陈旧源文件,
# 用 cl /MP 分组并发编译; 之后 nmake 发现对象已最新, 只做链接。
# 找不到 cl.exe (未运行 vcvarsall) 时直接成功返回, 让 nmake 走串行规则。

$ErrorActionPreference = 'Stop'
$root = (Get-Location).Path
$objDir = Join-Path $root 'build\obj'
if (!(Test-Path $objDir)) { New-Item -ItemType Directory -Path $objDir | Out-Null }

$cl = Get-Command cl.exe -ErrorAction SilentlyContinue
if (!$cl) { exit 0 }

function Get-MakeVar($name) {
	$m = Select-String -Path (Join-Path $root 'build.mk') -Pattern ("^" + [regex]::Escape($name) + "\s*=\s*(.+)$")
	if (!$m) { return $null }
	return $m.Matches[0].Groups[1].Value.Trim()
}

$cflags = Get-MakeVar 'CFLAGS'
$cxxflags = Get-MakeVar 'CXXFLAGS'
if (!$cflags -or !$cxxflags) { Write-Output '[cui] pcompile: CFLAGS not found, serial fallback'; exit 0 }

$headers = @()
$hdrFile = Join-Path $root 'build\headers.mk'
if (Test-Path $hdrFile) {
	$line = (Get-Content $hdrFile -Raw)
	$i = $line.IndexOf('=')
	if ($i -ge 0) {
		$headers = $line.Substring($i + 1).Trim() -split '\s+' | Where-Object { $_ }
	}
}
$maxHeader = [datetime]::MinValue
foreach ($h in $headers) {
	# headers.mk 里是绝对路径; 相对路径也兼容
	$p = $null
	if ([IO.Path]::IsPathRooted($h)) { $p = $h } else { $p = Join-Path $root $h }
	if (Test-Path $p) {
		$t = (Get-Item $p).LastWriteTimeUtc
		if ($t -gt $maxHeader) { $maxHeader = $t }
	}
}
$mkTime = (Get-Item (Join-Path $root "build.mk")).LastWriteTimeUtc
$grain = Join-Path $root 'build\grain_cso.h'
$grainTime = if (Test-Path $grain) { (Get-Item $grain).LastWriteTimeUtc } else { [datetime]::MinValue }

$sources = @()
$sources += Get-ChildItem (Join-Path $root 'src') -Recurse -Include *.c, *.cpp
$sources += Get-ChildItem (Join-Path $root 'samples') -Filter *.c
$sources += Get-ChildItem (Join-Path $root 'tests') -Filter *.c

$groups = @{
	'c' = New-Object System.Collections.ArrayList
	'cxx' = New-Object System.Collections.ArrayList
	'webview' = New-Object System.Collections.ArrayList
	'avx2' = New-Object System.Collections.ArrayList
}
$seen = @{}
$stale = 0
foreach ($f in $sources) {
	$name = [IO.Path]::GetFileNameWithoutExtension($f.Name)
	if ($seen.ContainsKey($name)) {
		Write-Output ("[cui] pcompile: duplicate object name " + $name + " (" + $f.FullName + ")")
		exit 1
	}
	$seen[$name] = $true
	$obj = Join-Path $objDir ($name + '.obj')
	if (!(Test-Path $obj)) {
		$need = $true
	} else {
		$ot = (Get-Item $obj).LastWriteTimeUtc
		$need = $ot -lt $f.LastWriteTimeUtc -or $ot -lt $maxHeader -or $ot -lt $mkTime
		if ($name -eq 'gfx_d2d' -and $ot -lt $grainTime) { $need = $true }
	}
	if (!$need) { continue }
	$stale++
	if ($name -eq 'ui_webview') { [void]$groups['webview'].Add($f.FullName) }
	elseif ($name -eq 'mem_avx2') { [void]$groups['avx2'].Add($f.FullName) }
	elseif ($f.Extension -eq '.cpp') { [void]$groups['cxx'].Add($f.FullName) }
	else { [void]$groups['c'].Add($f.FullName) }
}

if ($stale -eq 0) {
	Write-Output '[cui] pcompile: up to date'
	exit 0
}

$jobs = [int]$env:NUMBER_OF_PROCESSORS
if ($jobs -lt 1) { $jobs = 4 }
if ($jobs -gt 32) { $jobs = 32 }

$procs = @()
$tmp = @()
$specs = @(
	@{ key = 'c'; flags = $cflags; extra = ''; mp = $true },
	@{ key = 'cxx'; flags = $cxxflags; extra = ''; mp = $true },
	@{ key = 'webview'; flags = $cflags; extra = '/Ithird_party\webview2\include'; mp = $false },
	@{ key = 'avx2'; flags = $cflags; extra = '/arch:AVX2'; mp = $false }
)
$sw = [Diagnostics.Stopwatch]::StartNew()
foreach ($s in $specs) {
	$list = $groups[$s.key]
	if ($list.Count -eq 0) { continue }
	$rsp = Join-Path $root ('build\pcompile_' + $s.key + '.rsp')
	$log = Join-Path $root ('build\pcompile_' + $s.key + '.log')
	$err = Join-Path $root ('build\pcompile_' + $s.key + '.err')
	$tmp += $rsp, $log, $err
	$lines = New-Object System.Collections.Generic.List[string]
	$lines.Add($s.flags)
	if ($s.extra) { $lines.Add($s.extra) }
	if ($s.mp) { $lines.Add("/MP$jobs") }
	$lines.Add('/Fobuild\obj\')
	foreach ($src in $list) { $lines.Add('"' + $src + '"') }
	Set-Content -Path $rsp -Value $lines -Encoding Ascii
	$psi = New-Object System.Diagnostics.ProcessStartInfo
	$psi.FileName = $cl.Source
	$psi.Arguments = '@' + $rsp
	$psi.WorkingDirectory = $root
	$psi.UseShellExecute = $false
	$psi.CreateNoWindow = $true
	$psi.RedirectStandardOutput = $true
	$psi.RedirectStandardError = $true
	$p = New-Object System.Diagnostics.Process
	$p.StartInfo = $psi
	[void]$p.Start()
	$outTask = $p.StandardOutput.ReadToEndAsync()
	$errTask = $p.StandardError.ReadToEndAsync()
	$procs += @{ p = $p; out = $outTask; err = $errTask; key = $s.key }
}
$failed = $false
foreach ($it in $procs) {
	$it.p.WaitForExit()
	$outText = $it.out.Result
	$errText = $it.err.Result
	if ($it.p.ExitCode -ne 0) {
		$failed = $true
		Write-Output ("[cui] pcompile: group " + $it.key + " failed (rc=" + $it.p.ExitCode + ")")
		if ($outText) { $outText -split "`r?`n" | Select-Object -Last 40 }
		if ($errText) { $errText -split "`r?`n" | Select-Object -Last 40 }
	}
}
foreach ($t in $tmp) { if (Test-Path $t) { Remove-Item $t -Force } }
if ($failed) { exit 1 }
$sw.Stop()
Write-Output ("[cui] pcompile: {0} files in parallel (MP{1}, {2}ms)" -f $stale, $jobs, $sw.ElapsedMilliseconds)
exit 0
