# Le o log de uma tela MTS pelo cabo serial: reinicia e mostra o boot inteiro.
#
# POR QUE EXISTE: o eco pela internet da os NUMEROS do estado, mas o boot da os
# MOTIVOS - o aviso de fiacao, o codigo do begin(), a subida do painel e do
# cartao. Numa placa que nao transmite, e o boot que diz onde olhar.
#
# CUIDADO COM O CABO: so as portas com ponte CH343 (VID_1A86) carregam o console.
# A porta USB nativa do P4 (VID_303A) grava a placa mas NAO traz o Serial, porque
# o console sai pela UART0. Uma tela ligada por ela fica muda - custou meio dia
# descobrir isso.
#
#   .\tools\ler_tela.ps1              -> lista as portas e le todas as CH343
#   .\tools\ler_tela.ps1 -Port COM10  -> le so essa
#   .\tools\ler_tela.ps1 -Seg 40      -> espera mais tempo
param(
  [string]$Port = "",
  [int]$Seg = 25,
  [switch]$Tudo          # sem filtro: mostra o log cru inteiro
)

Write-Host "portas presentes:" -ForegroundColor Cyan
$portas = @()
Get-PnpDevice -Class Ports -PresentOnly | Where-Object { $_.FriendlyName -match '\(COM\d+\)' } | ForEach-Object {
  $nome = $_.FriendlyName
  $com  = ([regex]::Match($nome, '\((COM\d+)\)')).Groups[1].Value
  $id   = (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_InstanceId').Data
  $ch343 = $id -match 'VID_1A86'
  $nativa = $id -match 'VID_303A'
  $marca = if ($ch343) { "<- console OK" } elseif ($nativa) { "<- USB nativa: GRAVA mas nao fala" } else { "" }
  "  {0,-8} {1,-38} {2}" -f $com, $nome, $marca
  if ($ch343 -or ($nativa -and $Port -eq $com)) { $portas += $com }
}

if ($Port) { $portas = @($Port) }
if (-not $portas) { Write-Host "`nnenhuma porta com ponte serial. Ligue a tela pelo cabo com a pecinha preta." -ForegroundColor Yellow; exit 1 }

foreach ($p in $portas) {
  Write-Host "`n===================== $p =====================" -ForegroundColor Green
  try {
    $s = New-Object System.IO.Ports.SerialPort($p, 115200, 'None', 8, 'One')
    $s.ReadTimeout = 300
    $s.Open()
  } catch { Write-Host "nao abriu: $($_.Exception.Message)" -ForegroundColor Red; continue }

  # reinicia para pegar o boot desde o inicio
  $s.DtrEnable = $false; $s.RtsEnable = $true; Start-Sleep -Milliseconds 150; $s.RtsEnable = $false

  $sb = New-Object System.Text.StringBuilder
  $fim = (Get-Date).AddSeconds($Seg)
  while ((Get-Date) -lt $fim) {
    try { $null = $sb.Append($s.ReadExisting()) } catch {}
    Start-Sleep -Milliseconds 120
  }
  $s.Close()

  $txt = $sb.ToString()
  if (-not $txt.Trim()) { Write-Host "(silencio - essa porta nao traz o console desta placa)" -ForegroundColor Yellow; continue }

  if ($Tudo) { $txt.Trim(); continue }
  # o que interessa num diagnostico: radio, gps, painel, wifi, eco e panico
  ($txt -split "`n" | Where-Object {
      $_ -match 'radio|MTS|painel|lgfx|wifi|eco|gps|\[radio\]|rst:|Guru|abort|assert'
  }) -join "`n"
}
