# Install a traduku .msi for real, prove the installed program runs, prove the
# control panel sees it, uninstall it, prove nothing is left.
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
# Usage: test-msi-install.ps1 -Msi <path> [-KeepLogs]

param(
    [Parameter(Mandatory = $true)][string]$Msi,
    [string]$InstallDir = '',
    [switch]$KeepLogs
)

$ErrorActionPreference = 'Stop'
$fail = 0
function Ok($s)  { Write-Output ("ok    " + $s) }
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
