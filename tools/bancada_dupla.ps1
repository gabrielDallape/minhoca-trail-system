# Roteiro de bancada com DUAS telas pelo cabo: potencia baixa (antenas perto),
# CRIAR na lider, ENTRAR pela lista na seguidora, trilha, alerta ida e volta, e no
# fim volta a potencia de cada uma e sai do grupo. Usa os comandos seriais do mts
# (p, c, e, a, d, x). Grava tudo em <Saida>\<porta>.txt e imprime um resumo.
#
#   .\tools\bancada_dupla.ps1 -Lider COM9 -Seguidor COM11 -PwrLider 14 -PwrSeguidor 10
param([string]$Lider = 'COM9', [string]$Seguidor = 'COM11', [int]$PwrLider = 14, [int]$PwrSeguidor = 10,
      [int]$PwrBancada = 0, [int]$TrilhaSeg = 120, [string]$Saida = ".")
New-Item -ItemType Directory -Force $Saida | Out-Null
$Tela = @{}
foreach ($c in $Lider, $Seguidor) {
  $porta = New-Object System.IO.Ports.SerialPort $c, 115200
  $porta.DtrEnable = $false; $porta.RtsEnable = $false; $porta.NewLine = "`n"
  $porta.Open()
  $Tela[$c] = @{ p = $porta; arq = (Join-Path $Saida "$c.txt") }
  Set-Content $Tela[$c].arq "# $c" -Encoding utf8
}
function Le([int]$ms) {
  $fim = (Get-Date).AddMilliseconds($ms)
  do {
    foreach ($c in $Tela.Keys) {
      $t = $Tela[$c].p.ReadExisting()
      if ($t) { Add-Content $Tela[$c].arq ($t -replace "`r", "") -NoNewline -Encoding utf8 }
    }
    Start-Sleep -Milliseconds 200
  } while ((Get-Date) -lt $fim)
}
function Manda([string]$c, [string]$cmd) {
  Add-Content $Tela[$c].arq "`n>>> $(Get-Date -Format HH:mm:ss) ENVIADO: $cmd`n" -Encoding utf8
  $Tela[$c].p.Write($cmd + "`n")
}
try {
  Write-Host "1) potencia de bancada ($PwrBancada dBm) nas duas"
  Manda $Lider "p$PwrBancada"; Manda $Seguidor "p$PwrBancada"; Le 15000
  Manda $Lider "d"; Manda $Seguidor "d"; Le 3000
  Write-Host "2) lider CRIA o grupo"
  Manda $Lider "c"; Le 25000
  Manda $Seguidor "d"; Le 3000
  Write-Host "3) seguidor ENTRA pela lista"
  Manda $Seguidor "e"; Le 3000
  Write-Host "4) trilha por $TrilhaSeg s"
  $n = [math]::Ceiling($TrilhaSeg / 20)
  for ($k = 0; $k -lt $n; $k++) { Le 17000; Manda $Lider "d"; Manda $Seguidor "d"; Le 3000 }
  Write-Host "5) alerta do seguidor, ida e volta"
  Manda $Seguidor "a"; Le 20000; Manda $Seguidor "a"; Le 20000
  Write-Host "6) alerta do lider, ida e volta"
  Manda $Lider "a"; Le 20000; Manda $Lider "a"; Le 20000
  Manda $Lider "d"; Manda $Seguidor "d"; Le 3000
} finally {
  Write-Host "7) sai do grupo e devolve a potencia de cada uma ($PwrLider / $PwrSeguidor dBm)"
  Manda $Lider "x"; Manda $Seguidor "x"; Le 2000
  Manda $Lider "p$PwrLider"; Manda $Seguidor "p$PwrSeguidor"; Le 4000
  Manda $Lider "d"; Manda $Seguidor "d"; Le 3000
  foreach ($c in $Tela.Keys) { $Tela[$c].p.Close() }
}
