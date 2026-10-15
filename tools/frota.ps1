# FROTA - publica, grava em TODAS as telas ligadas e confere. Um comando so.
#
# POR QUE EXISTE: cada versao nova custava quatro comandos digitados na mao, um
# por tela, mais um para ler o resultado - e errar a porta ou esquecer uma tela
# era o normal, nao a excecao. Numa frota de tres isso ja incomoda; a partir dai
# vira fonte de erro (foi assim que duas telas foram para o campo em versoes
# DIFERENTES, em 2026-08-28).
#
# Ele descobre as telas sozinho: so portas com ponte CH343 (VID_1A86) carregam o
# console do P4. A USB nativa (VID_303A) grava mas nao fala, entao nao serve para
# conferir e fica de fora - ver o cabecalho do ler_tela.ps1.
#
#   .\tools\frota.ps1                -> publica, grava em todas, confere
#   .\tools\frota.ps1 -SemPublicar   -> so grava e confere (nao mexe na Vercel)
#   .\tools\frota.ps1 -SoConferir    -> nao grava nada, so le o estado das telas
param(
  [switch]$SemPublicar,
  [switch]$SoConferir,
  [int]$Seg = 30            # quanto observar cada tela na conferencia
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent

$hw = Get-Content "$repo\firmware\mts_p4\hardware.h" -Raw
if ($hw -notmatch '#define\s+MTS_VERSAO\s+(\d+)') { throw "MTS_VERSAO nao encontrado" }
$versao = [int]$Matches[1]
Write-Host "`n=== FROTA MTS - firmware v$versao ===" -ForegroundColor Cyan

# ---------------------------------------------------------------- as telas
$telas = @()
Get-PnpDevice -Class Ports -PresentOnly | Where-Object { $_.FriendlyName -match '\(COM\d+\)' } | ForEach-Object {
  $com = ([regex]::Match($_.FriendlyName, '\((COM\d+)\)')).Groups[1].Value
  $id  = (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_InstanceId').Data
  if ($id -match 'VID_1A86') { $telas += $com }
}
if (-not $telas) { Write-Host "nenhuma tela no cabo com ponte serial." -ForegroundColor Yellow; exit 1 }
Write-Host "telas encontradas: $($telas -join ', ')" -ForegroundColor Green

# ------------------------------------------------------------- 1. publicar
if (-not $SemPublicar -and -not $SoConferir) {
  Write-Host "`n--- publicando na Vercel ---" -ForegroundColor Cyan
  & "$repo\tools\publica_ota.ps1" | Select-String 'no ar|publicando|error'
}

# -------------------------------------------------------------- 2. gravar
if (-not $SoConferir) {
  foreach ($p in $telas) {
    Write-Host "`n--- gravando $p ---" -ForegroundColor Cyan
    & "$repo\tools\build.ps1" "firmware\mts_p4" -Board p4 -Upload -Port $p |
      Select-String 'gravado em|error:|falhou'
  }
  Write-Host "`nesperando as telas subirem..." -ForegroundColor DarkGray
  Start-Sleep -Seconds 12
}

# ----------------------------------------------------------- 3. conferir
# As portas podem ter mudado de numero apos a gravacao (o P4 re-enumera).
$telas = @()
Get-PnpDevice -Class Ports -PresentOnly | Where-Object { $_.FriendlyName -match '\(COM\d+\)' } | ForEach-Object {
  $com = ([regex]::Match($_.FriendlyName, '\((COM\d+)\)')).Groups[1].Value
  $id  = (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_InstanceId').Data
  if ($id -match 'VID_1A86') { $telas += $com }
}

# REINICIA ANTES DE OBSERVAR, por dois motivos que valem o incomodo:
#   1. o nome e a versao so aparecem no BANNER de boot. Sem reiniciar, a tabela
#      sai com "?" nas duas colunas e nao da para saber de qual tela se fala -
#      exatamente o problema que ja custou meio dia nesta frota.
#   2. os contadores comecam do zero em todas ao mesmo tempo, entao 'tx' e
#      'preso' viram TAXAS comparaveis. Sem isso, uma tela gravada 2 min antes
#      aparece com o triplo de tx e a comparacao nao diz nada.
Write-Host "`n--- reiniciando e observando por $Seg s ---" -ForegroundColor Cyan
$abertas = @()
foreach ($p in $telas) {
  try {
    $s = New-Object System.IO.Ports.SerialPort($p, 115200, 'None', 8, 'One')
    $s.ReadTimeout = 300; $s.Open()
    $s.DtrEnable = $false; $s.RtsEnable = $true; Start-Sleep -Milliseconds 120; $s.RtsEnable = $false
    $abertas += [PSCustomObject]@{ Com = $p; Port = $s; Buf = (New-Object System.Text.StringBuilder) }
  } catch { Write-Host "  $p nao abriu: $($_.Exception.Message)" -ForegroundColor Yellow }
}
$fim = (Get-Date).AddSeconds($Seg)
while ((Get-Date) -lt $fim) {
  foreach ($a in $abertas) { try { $null = $a.Buf.Append($a.Port.ReadExisting()) } catch {} }
  Start-Sleep -Milliseconds 150
}

# ------------------------------------------------------------- 4. veredito
Write-Host ""
"{0,-7} {1,-16} {2,-5} {3,-6} {4,-14} {5,-6} {6,-7} {7,-6} {8}" -f `
  "porta","nome","slot","ver","fonte","tx","preso","perdi","veredito"
Write-Host ("-" * 96)

$linhas = @()
foreach ($a in $abertas) {
  $t = $a.Buf.ToString()
  $a.Port.Close()

  $nome = if ($t -match 'nome=([^\r\n|]+)')      { $Matches[1].Trim() } else { "?" }
  $ver  = if ($t -match 'fw v(\d+)')             { "v$($Matches[1])"  } else { "?" }
  $rad  = @($t -split "`n" | Where-Object { $_ -match '\[radio\]' })
  if (-not $rad) {
    "{0,-7} {1,-16} {2,-5} {3,-6} {4,-14} {5,-6} {6,-7} {7,-6} {8}" -f `
      $a.Com,$nome,"-",$ver,"-","-","-","-","SEM LOG DE RADIO"
    continue
  }
  $u = $rad[-1]
  $slot  = if ($u -match 'slot=(\d+)')   { $Matches[1] } else { "?" }
  $fonte = if ($u -match 'fonte=([^|]+)'){ $Matches[1].Trim() } else { "?" }
  $tx    = if ($u -match 'tx=(\d+)')     { [int]$Matches[1] } else { 0 }
  $preso = if ($u -match 'preso=(\d+)')  { [int]$Matches[1] } else { 0 }
  $perdi = if ($u -match 'perdi=(\d+)')  { [int]$Matches[1] } else { 0 }
  $rx    = if ($u -match '\brx=(\d+)')   { [int]$Matches[1] } else { 0 }
  $conf  = if ($u -match 'conflito=(\d+)'){ [int]$Matches[1] } else { 0 }

  # Veredito por criterio, nao por impressao. O que decide se a rede esta boa:
  #   tx sobe (fala), rx ~= 2x tx com 3 nos (ouve todo mundo), preso e conflito 0.
  $v = @()
  if ($tx -eq 0)   { $v += "NAO TRANSMITE" }
  if ($preso -gt 0){ $v += "preso=$preso" }
  if ($conf -gt 0) { $v += "CONFLITO DE SLOT" }
  if ($tx -gt 0 -and $rx -eq 0) { $v += "nao ouve ninguem" }
  $vd = if ($v) { $v -join ", " } else { "ok (rx=$rx)" }

  "{0,-7} {1,-16} {2,-5} {3,-6} {4,-14} {5,-6} {6,-7} {7,-6} {8}" -f `
    $a.Com,$nome,$slot,$ver,$fonte,$tx,$preso,$perdi,$vd
  $linhas += [PSCustomObject]@{ tx=$tx; rx=$rx; preso=$preso; conf=$conf; ver=$ver }
}

# Uma frota so esta boa se TODAS estiverem na mesma versao e todas falando.
Write-Host ""
$vers = ($linhas | Select-Object -ExpandProperty ver -Unique)
if ($vers.Count -gt 1) { Write-Host "ATENCAO: versoes diferentes na frota ($($vers -join ', '))" -ForegroundColor Red }
$ruins = @($linhas | Where-Object { $_.tx -eq 0 -or $_.preso -gt 0 -or $_.conf -gt 0 })
if ($ruins.Count -eq 0 -and $linhas.Count -gt 0) {
  Write-Host "FROTA OK - $($linhas.Count) telas, todas transmitindo, sem travamento nem colisao" -ForegroundColor Green
} else {
  Write-Host "$($ruins.Count) de $($linhas.Count) telas com problema - ver a coluna veredito" -ForegroundColor Yellow
}
