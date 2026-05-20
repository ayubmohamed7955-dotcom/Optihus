# Build and run datahaandtering from repo root (local Web UI on http://127.0.0.1:8080).
# Usage:
#   .\scripts\dev-datahaandtering.ps1
#   .\scripts\dev-datahaandtering.ps1 -Foreground
#   .\scripts\dev-datahaandtering.ps1 -Wsl
#   .\scripts\dev-datahaandtering.ps1 -Watch
#   .\scripts\dev-datahaandtering.ps1 -NoRun
#   .\scripts\dev-datahaandtering.ps1 -SkipSign

param(
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [switch]$Watch,
    [switch]$NoRun,
    [switch]$Foreground,
    [switch]$Wsl,
    [switch]$SkipSign
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $RepoRoot

$ExeRel = if ($Config -eq "Debug") {
    "datahaandtering\Debug\datahaandtering.exe"
} else {
    "datahaandtering\datahaandtering.exe"
}
$ExePath = Join-Path $BuildDir $ExeRel
$DevCertSubject = "CN=Optihus Dev Code Sign"

function Write-ApplicationControlHelp {
    Write-Host ""
    Write-Host "Windows Application Control blokerede datahaandtering.exe (typisk Smart App Control)." -ForegroundColor Yellow
    Write-Host ""
    Write-Host "Muligheder:"
    Write-Host "  1) Kør scriptet igen (signerer exe med dev-cert automatisk efter build)"
    Write-Host "  2) Smart App Control fra: Indstillinger > Beskyttelse > Windows-sikkerhed >"
    Write-Host "     Styring af apps og browser > Smart App Control > Fra"
    Write-Host "  3) WSL: .\scripts\dev-datahaandtering.ps1 -Wsl"
    Write-Host "  4) Kun byg: .\scripts\dev-datahaandtering.ps1 -NoRun"
    Write-Host ""
}

function Stop-Datahaandtering {
    Get-Process -Name "datahaandtering" -ErrorAction SilentlyContinue | ForEach-Object {
        Write-Host "Stopper datahaandtering (PID $($_.Id))..."
        Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 400
}

function Sign-DevExecutable {
    param([string]$Path)
    if ($SkipSign) { return }
    $cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert -ErrorAction SilentlyContinue |
        Where-Object { $_.Subject -eq $DevCertSubject } |
        Select-Object -First 1
    if (-not $cert) {
        Write-Host "Opretter dev codesign-certifikat ($DevCertSubject)..."
        $cert = New-SelfSignedCertificate `
            -Type CodeSigningCert `
            -Subject $DevCertSubject `
            -CertStoreLocation Cert:\CurrentUser\My `
            -NotAfter (Get-Date).AddYears(5)
    }
    $sig = Set-AuthenticodeSignature -FilePath $Path -Certificate $cert
    if ($sig.Status -eq "Valid") {
        Write-Host "Exe signeret (dev-cert)."
    } else {
        Write-Host "Exe signeret med dev-cert (status: $($sig.Status))." -ForegroundColor DarkYellow
    }
}

function Build-Datahaandtering {
    if (-not (Test-Path (Join-Path $RepoRoot $BuildDir))) {
        Write-Host "CMake configure..."
        cmake -S . -B $BuildDir -DOPTIHUS_BUILD_DATAHAANDTERING=ON -DOPTIHUS_BUILD_MILJOESTYRING=OFF
    }
    Write-Host "Bygger datahaandtering ($Config)..."
    cmake --build $BuildDir --config $Config --target datahaandtering
    if (-not (Test-Path $ExePath)) {
        throw "Mangler executable: $ExePath"
    }
    Unblock-File -Path $ExePath -ErrorAction SilentlyContinue
    Sign-DevExecutable -Path $ExePath
}

function Convert-ToWslPath {
    param([Parameter(Mandatory = $true)][string]$WindowsPath)
    $normalized = $WindowsPath -replace '\\', '/'
    $mapped = wsl.exe wslpath -a -- "$normalized"
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($mapped)) {
        throw "Kunne ikke mappe Windows-sti til WSL: $WindowsPath"
    }
    return $mapped.Trim()
}

function Start-Datahaandtering-Wsl {
    $wsl = Get-Command wsl.exe -ErrorAction SilentlyContinue
    if (-not $wsl) {
        throw "wsl.exe ikke fundet. Installer WSL eller brug -Foreground / Smart App Control fra."
    }
    $wslRoot = Convert-ToWslPath -WindowsPath $RepoRoot
    if (-not $wslRoot) { throw "Kunne ikke mappe repo-sti til WSL." }
    Write-Host "WSL: bygger og starter (BUILD_DIR=build-wsl, cwd=$wslRoot)"
    Write-Host "Web UI: http://127.0.0.1:8080/"
    wsl.exe -e bash -lc "cd '$wslRoot' && BUILD_DIR=build-wsl exec ./scripts/dev-datahaandtering.sh"
}

function Start-Datahaandtering {
    if ($Wsl) {
        Start-Datahaandtering-Wsl
        return
    }

    Stop-Datahaandtering
    if (-not $SkipSign) {
        Sign-DevExecutable -Path $ExePath
    }
    Write-Host "Starter $ExePath (cwd=$RepoRoot)"
    Write-Host "Web UI: http://127.0.0.1:8080/"

    if ($Foreground) {
        Write-Host "Kører i denne terminal (Ctrl+C for at stoppe)."
        try {
            & $ExePath
        } catch {
            Write-ApplicationControlHelp
            throw
        }
        return
    }

    try {
        $proc = Start-Process -FilePath $ExePath -WorkingDirectory $RepoRoot -PassThru -ErrorAction Stop
        Write-Host "PID $($proc.Id)"
    } catch {
        Write-ApplicationControlHelp
        throw
    }
}

function Invoke-DevCycle {
    if ($Wsl) {
        if (-not $NoRun) {
            Start-Datahaandtering-Wsl
        } else {
            $wslRoot = Convert-ToWslPath -WindowsPath $RepoRoot
            wsl.exe -e bash -lc "cd '$wslRoot' && BUILD_DIR=build-wsl cmake -S . -B build-wsl -DOPTIHUS_BUILD_DATAHAANDTERING=ON -DOPTIHUS_BUILD_MILJOESTYRING=OFF 2>/dev/null || true; cd '$wslRoot' && BUILD_DIR=build-wsl cmake --build build-wsl --target datahaandtering"
        }
        return
    }
    Build-Datahaandtering
    if (-not $NoRun) {
        Start-Datahaandtering
    }
}

if ($Watch) {
    if ($Wsl) {
        throw "-Watch understøttes ikke sammen med -Wsl endnu."
    }
    $uiFile = Join-Path $RepoRoot "datahaandtering\src\web_ui_server.cpp"
    Invoke-DevCycle
    Write-Host "Watch: $uiFile (Ctrl+C for at stoppe)"
    $lastWrite = (Get-Item $uiFile).LastWriteTimeUtc
    while ($true) {
        Start-Sleep -Seconds 2
        $current = (Get-Item $uiFile).LastWriteTimeUtc
        if ($current -le $lastWrite) { continue }
        $lastWrite = $current
        Write-Host "`n--- Ændring registreret $(Get-Date -Format 'HH:mm:ss') ---"
        try {
            Invoke-DevCycle
        } catch {
            Write-Warning $_.Exception.Message
        }
    }
} else {
    Invoke-DevCycle
}

