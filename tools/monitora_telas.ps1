# Grava o serial de varias telas ao mesmo tempo, SEM reiniciar nenhuma (DTR e RTS
# ficam baixos ao abrir), com hora do PC em cada linha. Para bancada de 2 telas.
#
#   .\tools\monitora_telas.ps1 -Portas COM9,COM11 -Min 15 -Saida C:\pasta
#
# Um arquivo por porta: <Saida>\<porta>.txt
param([string[]]$Portas = @('COM9', 'COM11'), [int]$Min = 15, [string]$Saida = ".")
New-Item -ItemType Directory -Force $Saida | Out-Null
$abertas = @{}
foreach ($c in $Portas) {
  $p = New-Object System.IO.Ports.SerialPort $c, 115200
  $p.DtrEnable = $false; $p.RtsEnable = $false
  $p.Open()
  $abertas[$c] = @{ porta = $p; resto = ''; arq = (Join-Path $Saida "$c.txt") }
  Set-Content -Path $abertas[$c].arq -Value "# $c desde $(Get-Date -Format 'HH:mm:ss')" -Encoding utf8
}
$fim = (Get-Date).AddMinutes($Min)
try {
  while ((Get-Date) -lt $fim) {
    foreach ($c in $abertas.Keys) {
      $e = $abertas[$c]
      $txt = $e.resto + $e.porta.ReadExisting()
      $linhas = $txt -split "`n"
      $e.resto = $linhas[-1]
      if ($linhas.Count -gt 1) {
        $h = Get-Date -Format 'HH:mm:ss'
        $linhas[0..($linhas.Count - 2)] | ForEach-Object { "$h $($_.TrimEnd("`r"))" } | Add-Content -Path $e.arq -Encoding utf8
      }
    }
    Start-Sleep -Milliseconds 300
  }
} finally {
  foreach ($c in $abertas.Keys) { $abertas[$c].porta.Close() }
}
