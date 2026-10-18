param(
    [string]$OutFile = (Join-Path $PSScriptRoot "..\models\Spark-X2.5-4B-Q4_K_M.gguf")
)

$ErrorActionPreference = "Stop"

$out = [System.IO.Path]::GetFullPath($OutFile)
$dir = Split-Path -Parent $out
New-Item -ItemType Directory -Path $dir -Force | Out-Null

if (Test-Path -LiteralPath $out) {
    $size = (Get-Item -LiteralPath $out).Length
    if ($size -ge 2500000000) {
        Write-Host "模型已存在: $out ($([math]::Round($size / 1GB, 2)) GiB)"
        exit 0
    }
    Write-Host "检测到不完整文件 ($size bytes)，将重新下载"
}

$url = "https://modelscope.cn/models/XHToken/Spark-X2.5-4B-GGUF/resolve/master/Spark-X2.5-4B-Q4_K_M.gguf"
Write-Host "下载模型到 $out"

if (Get-Command curl.exe -ErrorAction SilentlyContinue) {
    & curl.exe -L --fail --retry 5 --retry-delay 3 -o $out $url
    if ($LASTEXITCODE -ne 0) { throw "curl 下载失败，退出码 $LASTEXITCODE" }
} else {
    Invoke-WebRequest -Uri $url -OutFile $out -TimeoutSec 1800
}

$size = (Get-Item -LiteralPath $out).Length
if ($size -lt 2500000000) {
    throw "模型文件不完整: $size bytes"
}

Write-Host "完成: $out ($([math]::Round($size / 1GB, 2)) GiB)"
