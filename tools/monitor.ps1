# Captura o serial de uma tela por N segundos e grava num arquivo.
#
# Uso:
#   .\tools\monitor.ps1 COM11                          # 30 s, imprime na tela
#   .\tools\monitor.ps1 COM11 -Segundos 60 -Saida log.txt
#   .\tools\monitor.ps1 COM11 -Enviar "s1"             # manda um comando antes de ler
#
# Por que existe: o firmware imprime telemetria a cada 5 s ([gps], [radio],
# quadro) e isso e a "blackbox de custo zero" ate o sd_bench de escrita liberar
# a de verdade. Em campo: celular OTG + app de terminal; na bancada: isto.
param(
  [Parameter(Mandatory = $true, Position = 0)] [string]$Porta,
  [int]$Segundos = 30,
  [string]$Saida = "",
  [string]$Enviar = ""
)
$ErrorActionPreference = 'Stop'

$p = New-Object System.IO.Ports.SerialPort $Porta, 115200, 'None', 8, 'One'
$p.ReadTimeout = 500
# DTR/RTS altos = niveis de repouso do circuito de auto-download (nao segura reset)
$p.DtrEnable = $true
$p.RtsEnable = $true
$p.Open()
try {
  if ($Enviar) { Start-Sleep -Milliseconds 300; $p.WriteLine($Enviar) }
  $fim = (Get-Date).AddSeconds($Segundos)
  $tudo = New-Object System.Text.StringBuilder
  while ((Get-Date) -lt $fim) {
    try {
      $linha = $p.ReadLine()
      [void]$tudo.AppendLine($linha)
      if (-not $Saida) { Write-Host $linha }
    } catch [TimeoutException] { }
  }
  if ($Saida) {
    $tudo.ToString() | Set-Content -Encoding UTF8 $Saida
    Write-Host "gravado: $Saida ($($tudo.Length) chars)"
  }
} finally { $p.Close() }
