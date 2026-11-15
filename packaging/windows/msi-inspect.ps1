# Read an .msi back through Windows Installer itself and assert what the
# package promises.
#
# Why read it back at all: candle and light exit 0 when they compiled my XML,
# not when the resulting database installs the thing I meant. The 0.4.0 zip
# lesson (ROADMAP.md §7.8 item 5) is the reason this file exists - a package
# checked only by the tool that wrote it is checked by that tool's optimism,
# and the reader has to be somebody else. Here the reader is msi.dll: the same
# engine that will run on the user's machine, asked questions about tables,
# which is the level at which MSI failures live (a KeyFile missing, a cabinet
# that is not embedded, an Upgrade row that quietly turns a version bump into
# two products both called traduku).
#
# Nothing here needs administrator rights: OpenDatabase(path, 0) is a read-only
# open of a file. The install itself is a different check, in build-msi.sh, and
# that script says out loud when it cannot run it.
#
# The numbers in this file come from a dump of a real package, not from a
# header. Two of them cost me a debugging round each, so they are recorded:
#
#   * the COM getter's `Property` index is one past the SDK's PID_ constants on
#     this machine. Template is 7, package type is 15, minimum InstallerVersion
#     is 14. A package type that reads back as a date is this mapping moving,
#     not the package changing - which is why the failure text quotes the value
#     it saw.
#   * File.FileName and Directory.DefaultDir store "short|long", with the long
#     name SECOND for files WiX harvested (the short name is a random 8.3 like
#     `7ujsa4hp.dll`), and "short|long" for directories. A check that reads
#     token 0 finds no Qt6Widgets.dll in a package that ships it. Every name
#     test below accepts any token, compared case-insensitively.
#
# Usage: msi-inspect.ps1 -Msi <path> [-ExpectFiles N] [-ExpectUpgradeCode <GUID>]
#                        [-ExpectMinVersionNT 603] [-Quiet]

param(
    [Parameter(Mandatory = $true)][string]$Msi,
    [int]$ExpectFiles = -1,
    [string]$ExpectUpgradeCode = '',
    [string]$ExpectMinVersionNT = '603',
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
$script:fail = 0
function Say($line) { if (-not $Quiet) { Write-Output $line } }
function Ok($line)  { Write-Output ("ok    " + $line) }
function Bad($line) { Write-Output ("FAIL  " + $line); $script:fail++ }

if (-not (Test-Path -LiteralPath $Msi)) { Write-Output "FAIL  no such file: $Msi"; exit 1 }
$msiFull = (Resolve-Path -LiteralPath $Msi).Path

# COM interop for Windows Installer needs InvokeMember: Execute/Fetch/OpenView
# take [out] parameters, and a plain PowerShell method call cannot pass them.
$installer = New-Object -ComObject WindowsInstaller.Installer
$db = $installer.GetType().InvokeMember('OpenDatabase', 'InvokeMethod', $null, $installer, @($msiFull, 0))
if ($null -eq $db) { Write-Output "FAIL  msi.dll cannot open $msiFull - it is not an installer database"; exit 1 }

function Get-Column($sql, $col) {
    $view = $db.GetType().InvokeMember('OpenView', 'InvokeMethod', $null, $db, $sql)
    $null = $view.GetType().InvokeMember('Execute', 'InvokeMethod', $null, $view, $null)
    $out = @()
    while ($true) {
        $rec = $view.GetType().InvokeMember('Fetch', 'InvokeMethod', $null, $view, $null)
        if ($null -eq $rec) { break }
        $out += $rec.GetType().InvokeMember('StringData', 'GetProperty', $null, $rec, @($col))
    }
    $null = $view.GetType().InvokeMember('Close', 'InvokeMethod', $null, $view, $null)
    return $out
}
function Has-Table($name) { (Get-Column "SELECT Name FROM _Tables" 1) -contains $name }
# "long|short" or "short|long": a name is present if any of its forms is it.
function Has-Name($forms, $want) {
    foreach ($f in $forms) {
        foreach ($tok in ($f -split '\|')) {
            if ($tok.ToLowerInvariant() -eq $want.ToLowerInvariant()) { return $true }
        }
    }
    return $false
}

# --- the payload --------------------------------------------------------------
if (-not (Has-Table 'File')) { Bad 'no File table - a package with no payload' }
else {
    $files = Get-Column "SELECT FileName FROM File" 1
    Say ("files: " + $files.Count)
    if (-not (Has-Name $files 'traduku.exe')) {
        Bad 'traduku.exe is not in the File table - the installer installs something with no program in it'
    } else { Ok ("traduku.exe is in the package (" + $files.Count + " files)") }

    if ($ExpectFiles -ge 0 -and $files.Count -ne $ExpectFiles) {
        Bad ("the File table has " + $files.Count + " rows, the staging tree has " + $ExpectFiles +
             " - the harvest and the tree disagree, and an old harvest is the usual reason")
    } elseif ($ExpectFiles -ge 0) {
        Ok ("file count matches the staging tree: " + $ExpectFiles)
    }

    # The runtime, by name: what a Qt/MinGW app dies without, one line each.
    # These are the same files check-portable-zip.sh insists on for the zip,
    # for the same reason - "installs fine, never starts" is a package failure.
    foreach ($need in @('traduku.exe','Qt6Core.dll','Qt6Gui.dll','Qt6Widgets.dll','Qt6Network.dll',
                        'Qt6Multimedia.dll','libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll')) {
        if (-not (Has-Name $files $need)) { Bad ("$need is not in the package") }
    }
    Ok 'the Qt and MinGW runtime files the app needs are all in the package'

    $dirNames = Get-Column "SELECT DefaultDir FROM Directory" 1
    if (-not (Has-Name $dirNames 'platforms')) {
        Bad 'no platforms\ directory - traduku.exe will install, start and die with "no Qt platform plugin could be initialized"'
    } else { Ok 'the platforms\ plugin directory is in the package' }
    foreach ($need in @('imageformats','styles','tls')) {
        if (-not (Has-Name $dirNames $need)) { Bad ("the plugin directory " + $need + "\ is missing (Qt will fall back and warn, and SVG icons will not render)") }
    }
}

# --- every component is in the feature, so every file is installed ------------
# A component that no feature references is not installed. That is the shape of
# "the harvest produced 31 components and the package installs 27 of them": the
# File table says the bytes are in the cabinet, the FeatureComponents table
# says where they go, and only the second one is a promise to the user.
if ((Has-Table 'Component') -and (Has-Table 'FeatureComponents')) {
    $comps = (Get-Column "SELECT Component FROM Component" 1).Count
    $refs  = (Get-Column "SELECT Feature_ FROM FeatureComponents" 1).Count
    if ($comps -ne $refs) {
        Bad ("$comps components, $refs feature/component references - " + ($comps - $refs) +
             " component(s) are in the package and in no feature, so they are never installed")
    } else { Ok ("all $comps components are referenced by a feature") }
}

# --- one file on the release page --------------------------------------------
if (Has-Table 'Media') {
    # An embedded cabinet is stored with a '#' prefix ("#cab1.cab").
    $cabs = Get-Column "SELECT Cabinet FROM Media" 1
    $embedded = @($cabs | Where-Object { $_ -like '#*' })
    if ($embedded.Count -eq 0) {
        Bad ("no embedded cabinet (Media.Cabinet = " + ($cabs -join ',') +
             ") - the release page would have to carry a .cab next to the .msi, and a user who copies only the .msi gets a failed install")
    } else { Ok 'the cabinet is embedded: the .msi is the whole package' }
} else { Bad 'no Media table - the package has no media' }

# --- the upgrade identity -----------------------------------------------------
if ($ExpectUpgradeCode -ne '') {
    if (-not (Has-Table 'Upgrade')) {
        Bad 'no Upgrade table - a new version installs beside the old one instead of replacing it'
    } else {
        $codes = Get-Column "SELECT UpgradeCode FROM Upgrade" 1 | ForEach-Object { $_ -replace '[{}]', '' }
        $want  = $ExpectUpgradeCode -replace '[{}]', ''
        if ($codes -notcontains $want) {
            Bad ("the Upgrade table does not carry " + $want + " (it has: " + ($codes -join ',') + ")")
        } else { Ok ("the Upgrade row is there: " + $want) }
        $distinct = $codes | Select-Object -Unique
        # WiX's MajorUpgrade writes two rows for one code: the upgrade of older
        # versions, and the downgrade refusal. Two rows, one code is the normal
        # shape. Two codes in one package is the shape nobody can reason about.
        if ($distinct.Count -ne 1) {
            Bad ("the Upgrade table carries " + $distinct.Count + " upgrade codes (" + ($distinct -join ', ') +
                 ") - one package upgrades one product")
        } else { Ok ("one upgrade code across " + $codes.Count + " Upgrade rows (MajorUpgrade writes the upgrade and the downgrade refusal)") }
    }
}

# --- what the control panel is told ------------------------------------------
$sum = $db.GetType().InvokeMember('SummaryInformation', 'GetProperty', $null, $db, @(3))
# 15 = package type on this engine: 2 = per-machine. traduku is a machine-wide
# tray app; a per-user package hides it from the machine's "Installed apps" and
# installs a global hotkey owner for one account only.
$pkgtype = $sum.GetType().InvokeMember('Property', 'GetProperty', $null, $sum, @(15))
if ($pkgtype -ne 2) {
    Bad ("package type is '" + $pkgtype + "', not 2 - this is not a per-machine package. (If the value looks like a date, the summary-property indexing on this machine has moved; see the header of this file.)")
} else { Ok 'per-machine package (one entry in "Installed apps" for the machine)' }
# 7 = template ("x64;1033"). A package that is not x64 lands in "Program Files
# (x86)" and carries 32-bit DLLs it should not have.
$tmpl = $sum.GetType().InvokeMember('Property', 'GetProperty', $null, $sum, @(7))
if ($tmpl -notmatch '^x64;') { Bad ("the template says '" + $tmpl + "' - not an x64 package") }
else { Ok ("platform: " + $tmpl.Split(';')[0]) }
# 14 = minimum Windows Installer version, which must match the Package element
# the installer was written against (500 = Wi 5.0, Windows 8+).
$wiver = $sum.GetType().InvokeMember('Property', 'GetProperty', $null, $sum, @(14))
if ([int]$wiver -lt 200) { Bad ("minimum Windows Installer version is " + $wiver + " - the package claims an engine older than the OS floor it installs on") }
else { Ok ("minimum Windows Installer: " + $wiver) }

# --- the OS floor, and the shortcut the user can decline ---------------------
if (Has-Table 'LaunchCondition') {
    $lc = (Get-Column "SELECT Condition FROM LaunchCondition" 1) -join ' AND '
    if ($lc -notmatch [regex]::Escape("VersionNT64 >= $ExpectMinVersionNT")) {
        Bad ("no VersionNT64 >= " + $ExpectMinVersionNT + " launch condition (table has: " + $lc + ")")
    } else { Ok ("OS floor: VersionNT64 >= " + $ExpectMinVersionNT) }
} else { Bad 'no LaunchCondition table - the package will install on an OS the app cannot run on' }

# WiX v3 stores a component condition in Component.Condition, not in the
# Condition table: the desktop shortcut is a component, so that is where its
# switch lives. A shortcut component with no condition is one the user cannot
# refuse, and the build script's documented CREATEDESKTOPSHORTCUT=0 would be a
# lie.
if (Has-Table 'Component') {
    $comp = Get-Column "SELECT Condition FROM Component" 1 | Where-Object { $_ }
    if (($comp -join ' ') -match 'CREATEDESKTOPSHORTCUT') { Ok 'the desktop shortcut is conditional (CREATEDESKTOPSHORTCUT=0 works)' }
    else { Bad 'the desktop shortcut has no condition - CREATEDESKTOPSHORTCUT=0 would be ignored' }
}

if ($script:fail -gt 0) { Write-Output ("msi-inspect: " + $script:fail + " problem(s) in " + (Split-Path -Leaf $Msi)); exit 1 }
Write-Output ("msi-inspect: " + (Split-Path -Leaf $Msi) + " is a package msi.dll agrees with")
exit 0
