# MEDE ANTENA - compara duas antenas em dB, usando o proprio radio como medidor.
#
# NAO mede potencia absoluta (isso exigiria instrumento). Mede a DIFERENCA, que e
# o que decide qual antena usar - e mede com estatistica, porque o RSSI oscila
# varios dB de pacote para pacote por multipercurso e uma leitura solta nao vale
# nada.
#
# COMO USAR - a disciplina aqui vale mais que o script:
#   1. Duas telas ligadas, PARADAS, em pontos MARCADOS no chao. Anote a
#      distancia. Campo aberto e muito melhor que dentro de casa.
#   2. Rode com um rotulo dizendo qual antena esta na tela que TRANSMITE:
#        .\tools\mede_antena.ps1 -Rotulo "fio soldado 5cm" -Port COM11
#   3. Troque SO a antena da outra tela. Nao mova nada, nao gire nada.
#   4. Rode de novo com outro rotulo:
#        .\tools\mede_antena.ps1 -Rotulo "ipx 915 5dbi" -Port COM11
#   5. A diferenca das medianas e o ganho em dB. Ele imprime a comparacao.
#
# O QUE INVALIDA A MEDIDA (e nao aparece no numero):
#   - mover qualquer uma das telas, mesmo 50 cm
#   - girar a antena (a polarizacao sozinha vale 20 dB)
#   - alguem passar entre as duas
#   - trocar a distancia entre as rodadas
# Se qualquer uma dessas mudar, a comparacao nao significa nada.
param(
  [Parameter(Mandatory = $true)][string]$Rotulo,
  [string]$Port = "",
  [int]$Seg = 60,
  [string]$Arquivo = ""
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
if (-not $Arquivo) { $Arquivo = Join-Path $repo "tools\medidas_antena.csv" }

# --------------------------------------------------------------- achar a tela
if (-not $Port) {
  Get-PnpDevice -Class Ports -PresentOnly | Where-Object { $_.FriendlyName -match '\(COM\d+\)' } | ForEach-Object {
    $c = ([regex]::Match($_.FriendlyName, '\((COM\d+)\)')).Groups[1].Value
    $id = (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName 'DEVPKEY_Device_InstanceId').Data
    if ($id -match 'VID_1A86' -and -not $Port) { $Port = $c }
  }
}
if (-not $Port) { Write-Host "nenhuma tela com ponte serial. Use -Port COMx." -ForegroundColor Red; exit 1 }
Write-Host "medindo em $Port por $Seg s   [$Rotulo]" -ForegroundColor Cyan

$s = New-Object System.IO.Ports.SerialPort($Port, 115200, 'None', 8, 'One')
$s.ReadTimeout = 300
$s.Open()

# LIGA o modo medida CONFERINDO, nao as cegas. O comando 'm' e um interruptor:
# mandado quando ele ja estava ligado, DESLIGA - e a medicao volta com zero
# amostras sem dizer o porque. Aconteceu. Pior: o laco da tela so le o serial a
# cada ~400 ms, entao mandar e fechar a porta em seguida nem sempre registra.
# Aqui a gente le a resposta e insiste ate ela dizer LIGADO.
function ModoMedida([System.IO.Ports.SerialPort]$porta, [bool]$ligar) {
  for ($i = 0; $i -lt 6; $i++) {
    $porta.DiscardInBuffer()
    $porta.Write("m`n")
    $eco = ""
    $lim = (Get-Date).AddSeconds(2)
    while ((Get-Date) -lt $lim -and $eco -notmatch 'modo medida') {
      Start-Sleep -Milliseconds 150
      try { $eco += $porta.ReadExisting() } catch {}
    }
    if ($eco -notmatch 'modo medida') { continue }             # nem leu o comando
    $estaLigado = $eco -match 'modo medida LIGADO'
    if ($estaLigado -eq $ligar) { return $true }               # ficou como queriamos
    # ficou ao contrario: o proximo laco manda de novo e inverte
  }
  return $false
}
if (-not (ModoMedida $s $true)) {
  Write-Host "nao consegui ligar o modo medida. A tela esta na TELA INICIAL?" -ForegroundColor Red
  $s.Close(); exit 1
}
Start-Sleep -Milliseconds 300
$s.DiscardInBuffer()

$sb = New-Object System.Text.StringBuilder
$fim = (Get-Date).AddSeconds($Seg)
$ultimo = -1
while ((Get-Date) -lt $fim) {
  try { $null = $sb.Append($s.ReadExisting()) } catch {}
  Start-Sleep -Milliseconds 100
  # so quando o segundo VIRA. Imprimir a cada 100 ms enche a saida de centenas de
  # linhas quando ela e capturada (o \r so funciona em terminal de verdade).
  $resta = [int]($fim - (Get-Date)).TotalSeconds
  if ($resta -ne $ultimo) { $ultimo = $resta; Write-Host -NoNewline "`r  faltam $resta s   " }
}
[void](ModoMedida $s $false)     # desliga, conferindo
$s.Close()
Write-Host "`r                       "

# ------------------------------------------------------------- as amostras
$amostras = @()
foreach ($l in ($sb.ToString() -split "`n")) {
  if ($l -match 'MEDIDA\s+(\d+)\s+(-?\d+)\s+(-?[\d\.]+)') {
    $amostras += [PSCustomObject]@{ Slot = [int]$Matches[1]; Rssi = [int]$Matches[2]; Snr = [double]$Matches[3] }
  }
}
if ($amostras.Count -lt 5) {
  Write-Host "so $($amostras.Count) amostras - insuficiente." -ForegroundColor Red
  Write-Host "confira: as duas telas ligadas, na TELA INICIAL (o log so sai la), e no mesmo grupo." -ForegroundColor Yellow
  exit 1
}

# Mediana, nao media: o RSSI tem caudas feias (um pacote no fundo de um desvanecimento
# puxa a media varios dB e nao representa o enlace).
function Mediana([double[]]$v) {
  $o = $v | Sort-Object
  $n = $o.Count
  if ($n % 2) { return $o[[int](($n - 1) / 2)] }
  return ([double]$o[$n / 2 - 1] + [double]$o[$n / 2]) / 2
}
$rssi = [double[]]($amostras.Rssi)
$med  = Mediana $rssi
$p10  = ($rssi | Sort-Object)[[int]($rssi.Count * 0.10)]
$p90  = ($rssi | Sort-Object)[[int]($rssi.Count * 0.90)]
$snrM = Mediana ([double[]]($amostras.Snr))

Write-Host "`n  amostras : $($amostras.Count)"
Write-Host "  RSSI     : mediana $med dBm   (p10 $p10 / p90 $p90)"
Write-Host "  SNR      : mediana $snrM dB"

# POR VIZINHO, e nao so o agregado. Isto e o que torna a comparacao confiavel:
# trocando a antena de UM no, o outro vira CONTROLE. Se o controle tambem mudar,
# a medida esta contaminada (alguem mexeu, alguem passou na frente) e o numero
# nao vale - sem esta coluna nao haveria como saber disso.
$porSlot = @{}
Write-Host "  por vizinho:"
foreach ($g in ($amostras | Group-Object Slot | Sort-Object Name)) {
  $m = Mediana ([double[]]($g.Group.Rssi))
  $porSlot["s$($g.Name)"] = $m
  "    slot {0}: mediana {1,6} dBm   ({2} amostras)" -f $g.Name, $m, $g.Count
}

# ------------------------------------------------------------- guardar e comparar
if (-not (Test-Path $Arquivo)) {
  "quando,rotulo,porta,amostras,rssi_mediana,rssi_p10,rssi_p90,snr_mediana,s0,s1,s2,s3" | Set-Content $Arquivo -Encoding utf8
}
# PONTO decimal, sempre. Neste sistema a virgula e o separador decimal, e -60,5
# escrito num CSV separado por virgula vira DUAS colunas - a tabela inteira anda
# de lado e as comparacoes passam a ler o numero errado. Cultura invariante em
# tudo que vai para o arquivo.
$inv = [System.Globalization.CultureInfo]::InvariantCulture
$n = { param($v) if ($null -eq $v -or $v -eq "") { "" } else { ([double]$v).ToString("0.##", $inv) } }
$cols = 0..3 | ForEach-Object { if ($porSlot.ContainsKey("s$_")) { & $n $porSlot["s$_"] } else { "" } }
$campos = @("$(Get-Date -Format s)", $Rotulo, $Port, $amostras.Count,
            (& $n $med), (& $n $p10), (& $n $p90), (& $n $snrM)) + $cols
($campos -join ',') | Add-Content $Arquivo -Encoding utf8

$hist = Import-Csv $Arquivo
Write-Host "`n--- medidas ja feitas ---" -ForegroundColor Cyan
"{0,-26} {1,-9} {2,-9} {3,-8} {4,-8} {5,-8} {6}" -f "rotulo","amostras","RSSI med","slot0","slot1","slot2","slot3"
foreach ($h in $hist) {
  "{0,-26} {1,-9} {2,-9} {3,-8} {4,-8} {5,-8} {6}" -f $h.rotulo, $h.amostras, $h.rssi_mediana, $h.s0, $h.s1, $h.s2, $h.s3
}

if ($hist.Count -ge 2) {
  $ant = $hist[-2]; $atu = $hist[-1]
  $d = [double]$atu.rssi_mediana - [double]$ant.rssi_mediana
  $sinal = if ($d -gt 0) { "+" } else { "" }
  Write-Host ""
  # O delta POR SLOT e o que separa "a antena melhorou" de "alguma coisa mudou no
  # ambiente". O slot cuja antena NAO foi trocada tem de ficar parado.
  Write-Host "  variacao por vizinho (o que nao mudou de antena e o controle):"
  foreach ($sc in @("s0","s1","s2","s3")) {
    if ($ant.$sc -and $atu.$sc) {
      $ds = [double]$atu.$sc - [double]$ant.$sc
      $marca = if ([math]::Abs($ds) -lt 3) { "estavel (controle ok)" } else { "MUDOU" }
      "    slot {0}: {1,6:N1} -> {2,6:N1}   =  {3,6:N1} dB   {4}" -f $sc.Substring(1), [double]$ant.$sc, [double]$atu.$sc, $ds, $marca
    }
  }
  Write-Host ""
  Write-Host ("  '{0}'  ->  '{1}'   =   {2}{3:N1} dB" -f $ant.rotulo, $atu.rotulo, $sinal, $d) -ForegroundColor Green
  # 6 dB dobram o alcance em espaco livre; em mata o expoente e maior e o ganho
  # em distancia e menor - por isso as duas contas, e nao uma so.
  $vezesLivre = [math]::Pow(10, $d / 20)
  $vezesMata  = [math]::Pow(10, $d / 35)
  Write-Host ("  alcance: x{0:N1} em espaco livre, x{1:N1} em mata fechada" -f $vezesLivre, $vezesMata)
  if ([math]::Abs($d) -lt 3) {
    Write-Host "  (menos de 3 dB e ruido de medicao - as duas antenas sao equivalentes)" -ForegroundColor Yellow
  }
}
