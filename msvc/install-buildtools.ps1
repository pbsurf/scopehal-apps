# Install (or complete an existing install of) Visual Studio 2022 Build Tools with everything the
# native MSVC build needs: MSVC x64 compiler, Windows SDK, CMake + Ninja, and vcpkg. No IDE, no GUI
# interaction. See devdoc/WindowsMSVC.md.
#
# Run from an elevated (Administrator) PowerShell:
#   powershell -ExecutionPolicy Bypass -File msvc\install-buildtools.ps1
#
# Options:
#   -InstallPath <dir>  install location for a new install (default: the installer's default,
#                       C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools)
#   -Quiet              no progress window (default shows progress but needs no input)
#   -Git                also install Git for Windows. Optional: vcpkg downloads its own git, but
#                       without git on PATH the ngscopeclient version string is a placeholder.
#   -Debugger           also install the x64 Debugging Tools for Windows from the Windows SDK (cdb,
#                       dbgsrv, kd, and the classic windbg.exe) into Windows Kits\10\Debuggers\x64.
#                       Useful for crash dumps and remote debugging. Not the WinDbg Store app.

param(
	[string]$InstallPath = '',
	[switch]$Quiet,
	[switch]$Git,
	[switch]$Debugger
)

$ErrorActionPreference = 'Stop'

$components = @(
	'Microsoft.VisualStudio.Workload.VCTools',
	'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
	'Microsoft.VisualStudio.Component.Windows11SDK.22621',
	'Microsoft.VisualStudio.Component.VC.CMake.Project',
	'Microsoft.VisualStudio.Component.Vcpkg'
)

$installerDir = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer"
$vswhere = "$installerDir\vswhere.exe"
$setup = "$installerDir\setup.exe"
$product = 'Microsoft.VisualStudio.Product.BuildTools'
$versionRange = '[17.0,18.0)'

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
	Write-Error 'Run this script from an elevated (Administrator) PowerShell.'
}

# TLS 1.2 for downloads on Windows PowerShell 5.1
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

function Get-BuildToolsPath {
	if (-not (Test-Path $vswhere)) { return $null }
	& $vswhere -products $product -version $versionRange -property installationPath | Select-Object -First 1
}

#Install or modify Build Tools

$addArgs = @()
foreach ($c in $components) { $addArgs += '--add', $c }
$uiArg = if ($Quiet) { '--quiet' } else { '--passive' }

$existing = Get-BuildToolsPath
if ($existing) {
	Write-Host "Adding components to existing Build Tools at: $existing"
	$exe = $setup
	$argList = @('modify', '--installPath', "`"$existing`"") + $addArgs + @($uiArg, '--norestart')
} else {
	$exe = "$env:TEMP\vs_BuildTools.exe"
	Write-Host "Downloading Build Tools bootstrapper to $exe"
	Invoke-WebRequest -UseBasicParsing -Uri 'https://aka.ms/vs/17/release/vs_BuildTools.exe' -OutFile $exe
	$argList = $addArgs + @($uiArg, '--wait', '--norestart')
	if ($InstallPath) { $argList += '--installPath', "`"$InstallPath`"" }
	Write-Host 'Installing Build Tools 2022 (this downloads several GB and can take a while)'
}

$p = Start-Process -FilePath $exe -ArgumentList $argList -Wait -PassThru
switch ($p.ExitCode) {
	0       { Write-Host 'Build Tools installer finished: success.' }
	3010    { Write-Host 'Build Tools installer finished: success, reboot required.' }
	default { Write-Warning "Build Tools installer exited with code $($p.ExitCode). Logs: $env:TEMP\dd_*.log" }
}

#Optionally install Git for Windows

if ($Git) {
	if (Get-Command git -ErrorAction SilentlyContinue) {
		Write-Host 'Git is already on PATH, skipping.'
	} else {
		$rel = Invoke-RestMethod -UseBasicParsing 'https://api.github.com/repos/git-for-windows/git/releases/latest'
		$asset = $rel.assets | Where-Object name -match '^Git-.*-64-bit\.exe$' | Select-Object -First 1
		$gitExe = "$env:TEMP\$($asset.name)"
		Write-Host "Downloading $($asset.name)"
		Invoke-WebRequest -UseBasicParsing -Uri $asset.browser_download_url -OutFile $gitExe
		$g = Start-Process -FilePath $gitExe -ArgumentList '/VERYSILENT','/NORESTART','/SUPPRESSMSGBOXES' -Wait -PassThru
		if ($g.ExitCode -eq 0) { Write-Host 'Git for Windows installed.' }
		else { Write-Warning "Git installer exited with code $($g.ExitCode)." }
	}
}

#Optionally install Debugging Tools for Windows

function Get-KitsRoot {
	(Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots' -ErrorAction SilentlyContinue).KitsRoot10
}

if ($Debugger) {
	$cdb = "$(Get-KitsRoot)Debuggers\x64\cdb.exe"
	if (Test-Path $cdb) {
		Write-Host "cdb is already installed: $cdb"
	} else {
		#Find the installed Windows SDK (Build Tools installs it as a bundle)
		$sdk = Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
				'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*' -ErrorAction SilentlyContinue |
			Where-Object { $_.DisplayName -match '^Windows Software Development Kit' -and $_.DisplayVersion } |
			Sort-Object { [version]$_.DisplayVersion } -Descending | Select-Object -First 1
		if (-not $sdk) {
			Write-Warning ('No Windows SDK found. Install "Debugging Tools for Windows" ' +
				'from https://developer.microsoft.com/windows/downloads/windows-sdk/')
		} else {
			#The SDK's own installer (winsdksetup.exe /features OptionId.WindowsDesktopDebuggers) can add
			#the debuggers, but it downloads from a copy of itself in a new temp dir each run, which
			#per-program firewalls block. So download the self-contained x64 debuggers MSI from the same
			#location here and install it directly. This installs only the x64 tools.
			$sdkVer = $sdk.DisplayVersion -replace '^10\.1\.', '10.0.'
			$fwlink = 'https://go.microsoft.com/fwlink/?prd=11966&pver=1.0&plcid=0x409&clcid=0x409' +
				"&ar=Windows10&sar=SDK&o1=$sdkVer"
			$req = [Net.HttpWebRequest]::Create($fwlink)
			$req.AllowAutoRedirect = $false
			$resp = $req.GetResponse()
			$root = $resp.Headers['Location']
			$resp.Close()
			if (-not $root) { Write-Error "Could not resolve the Windows SDK $sdkVer download location." }

			$msiName = 'X64 Debuggers And Tools-x64_en-us.msi'
			$msi = "$env:TEMP\$msiName"
			Write-Host "Downloading $msiName (SDK $sdkVer)"
			$savedProgress = $ProgressPreference
			$ProgressPreference = 'SilentlyContinue'	#the progress bar makes Invoke-WebRequest very slow
			Invoke-WebRequest -UseBasicParsing -Uri "$root/Installers/$msiName" -OutFile $msi
			$ProgressPreference = $savedProgress

			$msiLog = "$env:TEMP\winsdk-debuggers.log"
			$d = Start-Process -FilePath msiexec.exe -Wait -PassThru -ArgumentList @('/i', "`"$msi`"",
				'/qn', '/norestart', '/l*v', "`"$msiLog`"")
			$cdb = "$(Get-KitsRoot)Debuggers\x64\cdb.exe"
			if (($d.ExitCode -eq 0 -or $d.ExitCode -eq 3010) -and (Test-Path $cdb)) {
				Write-Host "Debugging Tools installed: $cdb"
				Remove-Item -Force $msi
			} else {
				Write-Warning "msiexec exited with code $($d.ExitCode). Log: $msiLog"
			}
		}
	}
}

#Verify

Write-Host ''
$path = Get-BuildToolsPath
if (-not $path) {
	Write-Error 'No Build Tools 2022 instance found after install.'
}
$missing = @()
foreach ($c in $components) {
	if (-not (& $vswhere -products $product -version $versionRange -requires $c -property installationPath)) {
		$missing += $c
	}
}
$msvc = Get-ChildItem "$path\VC\Tools\MSVC" -Directory -ErrorAction SilentlyContinue | ForEach-Object Name
Write-Host "Build Tools:   $path"
Write-Host "MSVC toolsets: $($msvc -join ', ')"
if ($missing.Count -eq 0) {
	Write-Host 'All required components are installed.'
	Write-Host ''
	Write-Host 'Next: open a developer prompt and configure, e.g. from cmd.exe:'
	Write-Host "  `"$path\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64"
	Write-Host '  cmake --preset windows-msvc'
} else {
	Write-Warning "Missing components: $($missing -join ', ')"
	exit 1
}
