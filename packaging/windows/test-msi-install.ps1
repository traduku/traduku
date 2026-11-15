# The .msi end-to-end: install it for real, prove the installed program runs, prove
# it translates against the repository mock, prove the control panel sees it,
# uninstall it, prove nothing is left.
#
# Stages, and why each is in this order (each one is a thing a package can get
# wrong while msiexec reports success):
#   1. msiexec /i returns 0 and traduku.exe is on disk
#   2. --selftest passes from the install dir with no Qt on PATH  -> payload complete
#   3. --translate reaches the mock endpoint                      -> network stack shipped
#   4. the HKLM uninstall entry exists with the right version     -> the user can see it
#   5. msiexec /x removes every file and the entry                -> the user can remove it
#
# This is the check that cannot be faked by reading tables: msi-inspect.ps1 can
# tell you the cabinet is embedded and the Upgrade row is right, and the
# install can still fail at the 27th file, on the user's machine, for a reason
# that only exists at install time (a file in use, a path too long, a custom
# action ordering, 1603 because the package is per-machine and the shell was
# not elevated - the last one is the reason this file asks for elevation and
# refuses without it, rather than reporting a red row from a test that never
# measured anything).
#
# Run elevated. build-msi.sh calls it when the shell is elevated and prints the
# skip with its reason when it is not; --prompt-uac there asks Windows for the
# elevation, which is the right thing on a desktop and the wrong thing in CI.
#
# Usage: test-msi-install.ps1 -Msi <path> [-MockPort N] [-NoTranslate] [-KeepLogs] [-Quiet]

param(
    [Parameter(Mandatory = $true)][string]$Msi,
    [string]$InstallDir = '',
    [int]$MockPort = 0,
    [switch]$NoTranslate,
    [switch]$KeepLogs,
    [switch]$Quiet
)

# Path separator, asked of the runtime rather than typed: this script is run by
# build-msi.sh from a shell that mixes MSYS and Windows paths, and the one
# literal that was hard-coded here pointed at tools\mock-openai.py by
# concatenating a variable that did not exist - it resolved to "tools" + "" +
# "mock-openai.py" and the stage reported "no mock server" on a machine that
# had one.
$B = [IO.Path]::DirectorySeparatorChar

$ErrorActionPreference = 'Stop'
$fail = 0
function Say($s) { if (-not $Quiet) { Write-Output $s } }   # used below; undefined here once, and the
function Ok($s)  { Write-Output ("ok    " + $s) }           # first elevated run would have thrown on it
function Bad($s) { Write-Output ("FAIL  " + $s); $script:fail++ }

$elevated = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $elevated) {
    Write-Output "FAIL  this shell is not elevated: a per-machine .msi cannot be installed here. msiexec answers 1603, and 'install failed with 1603' in a test log is a statement about the shell, not about the package."
    exit 1
}
if (-not (Test-Path -LiteralPath $Msi)) { Write-Output "FAIL  no such msi: $Msi"; exit 1 }
$msiFull = (Resolve-Path -LiteralPath $Msi).Path

# The product code is what uninstall takes, and it is generated per build
# (Product/@Id="*"), so it is read out of the package, not recomputed here.
$installer = New-Object -ComObject WindowsInstaller.Installer
$db = $installer.GetType().InvokeMember('OpenDatabase', 'InvokeMethod', $null, $installer, @($msiFull, 0))
function Column0($sql) {
    $v = $db.GetType().InvokeMember('OpenView', 'InvokeMethod', $null, $db, $sql)
    $null = $v.GetType().InvokeMember('Execute', 'InvokeMethod', $null, $v, $null)
    $r = $v.GetType().InvokeMember('Fetch', 'InvokeMethod', $null, $v, $null)
    $val = $null
    if ($null -ne $r) { $val = $r.GetType().InvokeMember('StringData', 'GetProperty', $null, $r, @(1)) }
    $null = $v.GetType().InvokeMember('Close', 'InvokeMethod', $null, $v, $null)
    return $val
}
$productCode = Column0 "SELECT Value FROM Property WHERE Property='ProductCode'"
$version = Column0 "SELECT Value FROM Property WHERE Property='ProductVersion'"
if (-not $productCode) { Write-Output 'FAIL  the package carries no ProductCode'; exit 1 }
Ok ("product code: $productCode (version $version)")

# A temp install dir: the test must not disturb a %ProgramFiles%\traduku that
# the developer installed by hand, and it must be a path the uninstall can be
# verified against without touching anything real.
if (-not $InstallDir) {
    $InstallDir = Join-Path ([IO.Path]::GetTempPath()) ("traduku-msi-test-" + [Guid]::NewGuid().ToString('N').Substring(0, 8) + "\traduku")
}
$logDir = Join-Path ([IO.Path]::GetTempPath()) ("traduku-msi-logs-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$installLog = Join-Path $logDir 'install.log'
$uninstallLog = Join-Path $logDir 'uninstall.log'

function Run-MsiExec($args, $log) {
    $p = Start-Process msiexec.exe -ArgumentList @($args + @('/qn', '/l*v', $log)) -Wait -PassThru
    return $p.ExitCode
}

Write-Output "installing into $InstallDir"
$rc = Run-MsiExec @('/i', $msiFull, "INSTALLFOLDER=$InstallDir") $installLog
if ($rc -ne 0) {
    Bad "msiexec /i exited $rc (see $installLog)"
    # 1603 is the generic "installation failed" and is almost always about the
    # environment, not the package: the two that bite this project are a
    # traduku.exe already running (Restart Manager) and a non-elevated shell.
    if ($rc -eq 1603) { Write-Output "      1603: is traduku running, and is this shell elevated?" }
    exit 1
}
Ok 'msiexec /i returned 0'

$exe = Join-Path $InstallDir 'traduku.exe'
if (-not (Test-Path -LiteralPath $exe)) { Bad "installed, and there is no traduku.exe at $exe"; exit 1 }
Ok "traduku.exe is in the install dir"

# The payload the package promises to be self-contained with: no Qt, no MinGW,
# only Windows itself on PATH. This is the same claim the portable zip makes,
# checked through the installer's own file layout.
#
# And no user settings: --selftest writes %LOCALAPPDATA%\traduku\config.ini,
# so the run gets a throwaway APPDATA/LOCALAPPDATA. A check that edits the
# configuration of the machine it is checking is not a check (ROADMAP.md
# §7.8 item 7 - on 2026-11-15 one of these runs replaced a configured install
# with the mock values it was testing with).
$systemRoot = [Environment]::GetEnvironmentVariable('WINDIR')
$purePath = (Join-Path $systemRoot 'System32') + ';' + $systemRoot
$iso = Join-Path ([IO.Path]::GetTempPath()) ('traduku-msi-config-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force -Path (Join-Path $iso 'Roaming'), (Join-Path $iso 'Local'), (Join-Path $iso 'config') | Out-Null
# The switch the program itself offers (Settings::defaultFilePath reads
# $TRADUKU_CONFIG, pinned by tst_settings). The APPDATA/LOCALAPPDATA pair below
# is a second layer for paths taken through QStandardPaths: on Windows those
# come from the shell's known-folder API and a child's environment does not
# move them, so TRADUKU_CONFIG is the part that works here.
New-Item -ItemType File -Force -Path (Join-Path $iso 'config\config.ini') | Out-Null
$prevConfig = $env:TRADUKU_CONFIG
$prevQpa = $env:QT_QPA_PLATFORM
$prevPath = $env:PATH
$prevAppData = $env:APPDATA
$prevLocal = $env:LOCALAPPDATA
$env:PATH = $purePath
$env:QT_QPA_PLATFORM = 'offscreen'
$env:TRADUKU_CONFIG = (Join-Path $iso 'config\config.ini')
$env:APPDATA = (Join-Path $iso 'Roaming')
$env:LOCALAPPDATA = (Join-Path $iso 'Local')
try {
    $selftest = & $exe --selftest 2>&1 | Out-String
} finally {
    $env:PATH = $prevPath
    $env:QT_QPA_PLATFORM = $prevQpa
    $env:TRADUKU_CONFIG = $prevConfig
    $env:APPDATA = $prevAppData
    $env:LOCALAPPDATA = $prevLocal
    Remove-Item -Recurse -Force $iso -ErrorAction SilentlyContinue
}
if ($selftest -notmatch 'SELFTEST PASSED') {
    Bad "the installed program does not pass its own --selftest with no Qt on PATH (this is the check that finds a runtime file the package did not carry):"
    Write-Output ($selftest -split "`n" | Select-Object -First 8)
    exit 1
}
Ok 'the installed traduku passes --selftest with a PATH that has no Qt in it'

# The user-visible promise of an installer: the app shows up in "Installed
# apps" with its version, and the entry points at something uninstallable.
$arp = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*' -ErrorAction SilentlyContinue |
       Where-Object { $_.PSChildName -eq $productCode -or $_.DisplayName -eq 'traduku' } |
       Select-Object -First 1
if (-not $arp) { Bad 'the install put no traduku entry in the machine-wide uninstall list' }
else {
    if ($arp.DisplayVersion -ne $version) { Bad ("'Installed apps' shows version " + $arp.DisplayVersion + ", the package is " + $version) }
    else { Ok ("'Installed apps' shows traduku " + $arp.DisplayVersion + ' (Remove is wired: ' + [bool]$arp.UninstallString + ')') }
    # The licence text: GPL-3.0-or-later, per README. The dialog the user
    # agreed to is in the package; the Readme/RegisteredOrganization fields are
    # what Settings shows, and an empty one is a package that never says what
    # it is.
    if ($arp.DisplayIcon) { Say ("icon: " + $arp.DisplayIcon) }
}

# --- the installed program does the one thing the product does ----------------
# --selftest proves the payload is complete. This proves the *installed*
# artifact answers a translation request: the mock OpenAI server from the
# repository (tools/mock-openai.py) is started on a loopback port, one isolated
# config points the installed exe at it, and `--translate` must stream back the
# mock's answer. Everything runs with a PATH that has no Qt in it, so the
# network stack that answers is the one the package carried (Qt6Network.dll) -
# a package that installs the exe and forgets the network plugin passes
# --selftest and cannot translate, and this is the row that sees the
# difference.
#
# No user settings, no user endpoint: the config is a temp file ($env:
# TRADUKU_CONFIG, the switch the program documents), and the endpoint is
# 127.0.0.1, so nothing here can reach a real API or spend a real key.
if (-not $NoTranslate) {
    $root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
    $mock = Join-Path $root ('tools' + $B + 'mock-openai.py')
    $py = $null
    foreach ($cand in @('python', 'python3', 'py')) {
        $g = Get-Command $cand -ErrorAction SilentlyContinue
        if ($g) { $py = $g.Source; break }
    }
    if (-not (Test-Path -LiteralPath $mock)) { Bad "no $mock - cannot run the translate stage" }
    elseif (-not $py) { Bad 'no python on PATH - cannot start the mock endpoint (pass -NoTranslate for a packages-only run)' }
    else {
        if ($MockPort -le 0) { $MockPort = 8931 + (Get-Random -Maximum 400) }
        $mockLog = Join-Path $logDir 'mock.log'
        $mockProc = Start-Process -FilePath $py -ArgumentList @($mock, "$MockPort") `
            -PassThru -WindowStyle Hidden -RedirectStandardOutput $mockLog -RedirectStandardError (Join-Path $logDir 'mock.err')
        try {
            $up = $false
            for ($i = 0; $i -lt 40 -and -not $up; $i++) {
                Start-Sleep -Milliseconds 250
                try {
                    $r = Invoke-WebRequest -UseBasicParsing -Uri ('http://127.0.0.1:' + $MockPort + '/v1/models') -TimeoutSec 2
                    if ($r.StatusCode -eq 200) { $up = $true }
                } catch { }
            }
            if (-not $up) { Bad "the mock endpoint never answered on port $MockPort (see $logDir)" }
            else {
                $cfg = Join-Path $iso 'translate.ini'
                @(
                    '[General]',
                    ('ApiUrl=http://127.0.0.1:' + $MockPort + '/v1'),
                    'ApiKey=e2e-only-not-a-secret',
                    'Model=test-model',
                    'TargetLanguage=nl'
                ) | Set-Content -LiteralPath $cfg -Encoding ASCII
                $probe = 'hello from the installer test'
                $prevPath2 = $env:PATH; $prevQpa2 = $env:QT_QPA_PLATFORM
                $env:PATH = $purePath; $env:QT_QPA_PLATFORM = 'offscreen'
                try { $tr = & $exe --translate --text $probe --config $cfg 2>&1 | Out-String }
                finally { $env:PATH = $prevPath2; $env:QT_QPA_PLATFORM = $prevQpa2 }
                # The mock replies "[MOCK -> Nederlands (Dutch)] <text>": the
                # prefix proves the request arrived and streamed, the echo
                # proves the text is the one we sent, and the language name
                # proves TargetLanguage from the config reached the prompt.
                if ($tr -notmatch '\[MOCK -> ') {
                    Bad ('the installed program did not translate (reply: [' + ($tr.Trim()) + ']) - a package with the exe and not Qt6Network.dll passes --selftest and cannot talk to an API')
                } elseif ($tr -notmatch [regex]::Escape($probe)) {
                    Bad ("the translation did not echo the probe text: [" + ($tr.Trim()) + ']')
                } elseif ($tr -notmatch 'Dutch') {
                    Bad ("the reply does not name the configured target language: [" + ($tr.Trim()) + ']')
                } else {
                    Ok ('the installed traduku translated through the network stack the package carries: ' + ($tr.Trim()))
                }
            }
        } finally {
            Stop-Process -Id $mockProc.Id -Force -ErrorAction SilentlyContinue
        }
    }
}

# Autostart is the app's setting, not the installer's (see product.wxs). The
# install must not write the Run key: a user who turned autostart off and then
# upgrades should not find it on again.
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
if ((Test-Path $runKey) -and ((Get-ItemProperty $runKey -ErrorAction SilentlyContinue).PSObject.Properties.Name -contains 'traduku')) {
    Write-Output "note  HKCU Run has a traduku entry - that is the app's own autostart setting, written by the Settings window, not by this install (the install runs as SYSTEM here and writes no HKCU)."
}

Write-Output "uninstalling $productCode"
$rc = Run-MsiExec @('/x', $productCode) $uninstallLog
if ($rc -ne 0) { Bad "msiexec /x exited $rc (see $uninstallLog)"; exit 1 }
Ok 'msiexec /x returned 0'

if (Test-Path -LiteralPath $exe) { Bad "uninstall left traduku.exe behind at $exe - an app the user cannot remove is worse than no app" }
else { Ok 'the install dir has no traduku.exe after uninstall' }
$left = @(Get-ChildItem -LiteralPath $InstallDir -Recurse -File -ErrorAction SilentlyContinue)
if ($left.Count -gt 0) { Bad ("uninstall left " + $left.Count + " file(s): " + (($left | Select-Object -First 5 | ForEach-Object Name) -join ', ')) }
else { Ok 'uninstall removed every file it installed' }
$arp2 = Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*' -ErrorAction SilentlyContinue |
        Where-Object { $_.PSChildName -eq $productCode }
if ($arp2) { Bad 'the "Installed apps" entry survived the uninstall' } else { Ok 'the "Installed apps" entry is gone' }

if (-not $KeepLogs) { Remove-Item -Recurse -Force $logDir -ErrorAction SilentlyContinue }
else { Write-Output "logs: $logDir" }

if ($script:fail -gt 0) { Write-Output "test-msi-install: $script:fail problem(s)"; exit 1 }
Write-Output 'test-msi-install: the package installs, runs, appears, and uninstalls clean'
exit 0
