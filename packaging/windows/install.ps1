# Install (or update) CedarLogic on Windows, from the newest release:
#
#   irm https://cedarlogic.netlify.app/install.ps1 | iex
#
# Finds the newest full release of leviholliday/CedarLogic-Releases that has a
# Windows installer, downloads the one for this PC (ARM64 on Windows on ARM,
# else x64), checks it, installs it silently and starts CedarLogic. The
# installer is per-user (no administrator): CedarLogic goes in
# %LOCALAPPDATA%\Programs\CedarLogic, the Start menu and Settings > Apps.
#
# The check: the file's size and SHA-256 must be the ones GitHub records for
# the release's file, and when the release has a signed list (SHA256SUMS and
# SHA256SUMS.sig, signed with CedarLogic's update key: the same key and format
# as the apps' updaters, update-signing/UpdateSignature.h), the signature must
# check out and the list must name this file with that size and SHA-256.
# Anything off and nothing is installed.
#
# With options (run it as a script block):
#   & ([scriptblock]::Create((irm https://cedarlogic.netlify.app/install.ps1))) -DryRun
#     -DryRun     everything but installing: find, download, check, then stop
#     -Version V  that release (v<V>) instead of the newest
#     -Arch A     x64 or ARM64 instead of this PC's
#     -NoLaunch   don't start CedarLogic afterwards
#     -SelfTest   check the signature code against RFC 8032's test vectors, then stop
#     -Tag T      that release tag, pre-releases too, with or without a version in the
#                 file names (to try this now on the signed rolling test build:
#                 -Repo leviholliday/CedarLogic -Tag windows-native-testing -DryRun)
#     -Repo O/R   another repo than leviholliday/CedarLogic-Releases
param(
	[switch]$DryRun,
	[string]$Version = '',
	[ValidateSet('x64', 'ARM64')][string]$Arch,
	[switch]$NoLaunch,
	[switch]$SelfTest,
	[string]$Tag = '',
	[string]$Repo = 'leviholliday/CedarLogic-Releases'
)

function Install-CedarLogicFromRelease {
	param([switch]$DryRun, [string]$Version, [string]$Arch, [switch]$NoLaunch, [switch]$SelfTest, [string]$Tag, [string]$Repo)

	$ErrorActionPreference = 'Stop'
	$ProgressPreference = 'SilentlyContinue'   # Invoke-WebRequest is many times faster without its progress bar
	if (-not $Repo) { $Repo = 'leviholliday/CedarLogic-Releases' }
	$Header = 'CedarLogic preview manifest 1'
	# The update key's public half (Ed25519), as in scripts/sign-preview.sh (PUBHEX).
	$PublicKeyHex = '4184daba2830c1db416be7869a7d873788170ea1c00104abcbfd018d7fe3cf40'

	function Say([string]$m) { Write-Host $m }
	function Fail([string]$m) { throw "CedarLogic wasn't installed: $m" }

	# ---- Ed25519 (RFC 8032), verify only ---------------------------------------
	# Windows has no Ed25519 of its own, so this is the textbook algorithm on
	# .NET's BigInteger: slow (a second or two), but it runs once.
	try { Add-Type -AssemblyName System.Numerics } catch { }   # already there in PowerShell 7
	$BI = [System.Numerics.BigInteger]
	$P = $BI::Subtract($BI::Pow($BI::new(2), 255), $BI::new(19))
	$Lq = $BI::Add($BI::Pow($BI::new(2), 252), $BI::Parse('27742317777372353535851937790883648493'))
	$Zero = $BI::Zero
	$One = $BI::One

	function ModP($x) { $r = $BI::Remainder($x, $P); if ($r.Sign -lt 0) { $r = $BI::Add($r, $P) }; $r }
	function MulP($a, $b) { ModP ($BI::Multiply($a, $b)) }
	function AddP($a, $b) { ModP ($BI::Add($a, $b)) }
	function SubP($a, $b) { ModP ($BI::Subtract($a, $b)) }
	function InvP($a) { $BI::ModPow($a, $BI::Subtract($P, $BI::new(2)), $P) }
	function Same($a, $b) { $BI::Compare($a, $b) -eq 0 }

	# Little-endian bytes (from $off, $len of them) as a non-negative number.
	function FromLE([byte[]]$bytes, [int]$off, [int]$len) {
		$b = New-Object byte[] ($len + 1)
		[Array]::Copy($bytes, $off, $b, 0, $len)
		$BI::new($b)
	}
	function To32($n) {
		$b = $n.ToByteArray()
		$out = New-Object byte[] 32
		[Array]::Copy($b, 0, $out, 0, [Math]::Min($b.Length, 32))
		return , $out
	}
	function HexBytes([string]$h) {
		$b = New-Object byte[] ($h.Length / 2)
		for ($i = 0; $i -lt $b.Length; $i++) { $b[$i] = [Convert]::ToByte($h.Substring(2 * $i, 2), 16) }
		return , $b
	}

	$Dc = MulP ($BI::Subtract($P, $BI::new(121665))) (InvP ($BI::new(121666)))   # d = -121665/121666
	$D2 = AddP $Dc $Dc
	$SqrtM1 = $BI::ModPow($BI::new(2), $BI::Divide($BI::Subtract($P, $One), $BI::new(4)), $P)
	$E58 = $BI::Divide($BI::Subtract($P, $BI::new(5)), $BI::new(8))

	# Points in extended coordinates (X, Y, Z, T), x = X/Z, y = Y/Z, x*y = T/Z.
	function PtAdd($p1, $p2) {
		$a = MulP (SubP $p1[1] $p1[0]) (SubP $p2[1] $p2[0])
		$b = MulP (AddP $p1[1] $p1[0]) (AddP $p2[1] $p2[0])
		$c = MulP (MulP $p1[3] $D2) $p2[3]
		$d = MulP (AddP $p1[2] $p1[2]) $p2[2]
		$e = SubP $b $a; $f = SubP $d $c; $g = AddP $d $c; $h = AddP $b $a
		return @((MulP $e $f), (MulP $g $h), (MulP $f $g), (MulP $e $h))
	}
	function PtMul($k, $pt) {
		$q = @($Zero, $One, $One, $Zero)
		$kb = $k.ToByteArray()
		for ($i = 255; $i -ge 0; $i--) {
			$q = PtAdd $q $q
			$byte = $i -shr 3
			if ($byte -lt $kb.Length -and (($kb[$byte] -shr ($i -band 7)) -band 1)) { $q = PtAdd $q $pt }
		}
		return $q
	}
	function PtEncode($pt) {
		$zi = InvP $pt[2]
		$x = MulP $pt[0] $zi
		$y = MulP $pt[1] $zi
		$out = To32 $y
		if (-not $x.IsEven) { $out[31] = $out[31] -bor 0x80 }
		return , $out
	}
	# RFC 8032 5.1.3; $null when the bytes aren't a point.
	function PtDecode([byte[]]$s) {
		if ($s.Length -ne 32) { return $null }
		$c = New-Object byte[] 32
		[Array]::Copy($s, $c, 32)
		$x0 = ($c[31] -shr 7) -band 1
		$c[31] = $c[31] -band 0x7f
		$y = FromLE $c 0 32
		if ($BI::Compare($y, $P) -ge 0) { return $null }
		$y2 = MulP $y $y
		$u = SubP $y2 $One
		$v = AddP (MulP $Dc $y2) $One
		$v3 = MulP (MulP $v $v) $v
		$v7 = MulP (MulP $v3 $v3) $v
		$x = MulP (MulP $u $v3) ($BI::ModPow((MulP $u $v7), $E58, $P))
		$vx2 = MulP $v (MulP $x $x)
		if (-not (Same $vx2 $u)) {
			if (Same $vx2 (SubP $Zero $u)) { $x = MulP $x $SqrtM1 } else { return $null }
		}
		if ($x.IsZero -and $x0 -eq 1) { return $null }
		if ([int](-not $x.IsEven) -ne $x0) { $x = SubP $Zero $x }
		return @($x, $y, $One, (MulP $x $y))
	}
	$Base = PtDecode (HexBytes ('58' + ('66' * 31)))

	function Test-Ed25519([byte[]]$message, [byte[]]$signature, [byte[]]$publicKey) {
		if ($signature.Length -ne 64 -or $publicKey.Length -ne 32) { return $false }
		$A = PtDecode $publicKey
		if ($null -eq $A) { return $false }
		$R = New-Object byte[] 32
		[Array]::Copy($signature, 0, $R, 0, 32)
		$S = FromLE $signature 32 32
		if ($BI::Compare($S, $Lq) -ge 0) { return $false }
		$buf = New-Object byte[] (64 + $message.Length)
		[Array]::Copy($R, 0, $buf, 0, 32)
		[Array]::Copy($publicKey, 0, $buf, 32, 32)
		if ($message.Length -gt 0) { [Array]::Copy($message, 0, $buf, 64, $message.Length) }
		$sha = [System.Security.Cryptography.SHA512]::Create()
		try { $hash = $sha.ComputeHash($buf) } finally { $sha.Dispose() }
		$h = $BI::Remainder((FromLE $hash 0 64), $Lq)
		# [S]B - [h]A must be R.
		$hA = PtMul $h $A
		$negHA = @((SubP $Zero $hA[0]), $hA[1], $hA[2], (SubP $Zero $hA[3]))
		$check = PtEncode (PtAdd (PtMul $S $Base) $negHA)
		for ($i = 0; $i -lt 32; $i++) { if ($check[$i] -ne $R[$i]) { return $false } }
		return $true
	}

	if ($SelfTest) {
		# RFC 8032 section 7.1, tests 1 and 2, and the same with one bit changed.
		$t1k = HexBytes 'd75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a'
		$t1s = HexBytes 'e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b'
		$t2k = HexBytes '3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c'
		$t2s = HexBytes '92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00'
		$t2m = [byte[]]@(0x72)
		$ok = (Test-Ed25519 ([byte[]]@()) $t1s $t1k) -and (Test-Ed25519 $t2m $t2s $t2k)
		$bad = $t2s.Clone(); $bad[5] = $bad[5] -bxor 1
		$ok = $ok -and -not (Test-Ed25519 $t2m $bad $t2k) -and -not (Test-Ed25519 ([byte[]]@(0x73)) $t2s $t2k) -and -not (Test-Ed25519 $t2m $t2s $t1k)
		if ($ok) { Say 'Self-test passed: Ed25519 verifies RFC 8032''s signatures and refuses changed ones.' }
		else { throw 'Self-test FAILED: the signature check is wrong. Please report this.' }
		return
	}

	# ---- This PC -------------------------------------------------------------------
	if ($env:OS -ne 'Windows_NT') { Fail 'this installs the Windows app. On a Mac or Linux, see https://cedarlogic.netlify.app' }
	if ([Environment]::OSVersion.Version.Major -lt 10) { Fail 'CedarLogic needs Windows 10 or 11.' }
	if (-not $Arch) {
		# The system's own architecture, not this PowerShell's: Windows on ARM
		# runs x64 PowerShell too. The registry has the real one.
		$native = $null
		try { $native = (Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment' -Name PROCESSOR_ARCHITECTURE -ErrorAction Stop).PROCESSOR_ARCHITECTURE } catch { }
		if (-not $native) { $native = if ($env:PROCESSOR_ARCHITEW6432) { $env:PROCESSOR_ARCHITEW6432 } else { $env:PROCESSOR_ARCHITECTURE } }
		switch -Regex ($native) {
			'^(AMD64|x64)$' { $Arch = 'x64' }
			'^ARM64$' { $Arch = 'ARM64' }
			default { Fail "CedarLogic needs 64-bit Windows (an x64 or ARM64 PC); this one is $native." }
		}
	}
	try {
		[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
	} catch { }

	# ---- The release -------------------------------------------------------------
	Say "Looking for the newest CedarLogic for Windows ($Arch)..."
	$apiHeaders = @{ 'Accept' = 'application/vnd.github+json' }
	$ua = 'CedarLogic-install.ps1'   # -UserAgent: Windows PowerShell 5.1 won't take User-Agent in -Headers
	try {
		if ($Tag) { $releases = @(Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/tags/$Tag" -Headers $apiHeaders -UserAgent $ua -UseBasicParsing) }
		else { $releases = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases?per_page=30" -Headers $apiHeaders -UserAgent $ua -UseBasicParsing }
	} catch {
		if ($Tag -and $_.Exception.Message -match '404') { Fail "there's no release $Tag in $Repo." }
		$why = $_.Exception.Message
		if ($why -match '403|429|rate limit') { Fail "GitHub is limiting how often this network can ask (many installs at once, as in a classroom?). Wait an hour, or download the installer from https://cedarlogic.netlify.app ($why)" }
		Fail "couldn't reach GitHub to find the release ($why). Check the internet connection and try again."
	}
	# Versioned release files are CedarLogic-0.4-Windows-Setup-x64.exe; the
	# rolling test build's have no version (only with -Tag).
	$pattern = if ($Tag) { '^CedarLogic-(\d+(\.\d+)+-)?Windows-Setup-(x64|ARM64)\.exe$' } else { '^CedarLogic-\d+(\.\d+)+-Windows-Setup-(x64|ARM64)\.exe$' }
	$release = $null; $asset = $null
	foreach ($r in @($releases)) {
		if ($r.draft -or ($r.prerelease -and -not $Tag)) { continue }
		if ($Version -and $r.tag_name -ne "v$Version" -and $r.tag_name -ne $Version) { continue }
		$setups = @($r.assets | Where-Object { $_.name -match $pattern })
		if ($setups.Count -eq 0) { continue }   # a Mac-only release
		$asset = $setups | Where-Object { $_.name -match "-Windows-Setup-$Arch\.exe$" } | Select-Object -First 1
		if (-not $asset -and $Arch -eq 'ARM64') {
			# Windows 11 on ARM runs the x64 build too.
			$asset = $setups | Where-Object { $_.name -match '-Windows-Setup-x64\.exe$' } | Select-Object -First 1
			if ($asset) { Say 'No ARM64 installer in this release; using the x64 one (Windows on ARM runs it).' }
		}
		if ($asset) { $release = $r; break }
	}
	if (-not $release) {
		if ($Version) { Fail "there's no full release v$Version with a Windows installer for $Arch." }
		Fail "there's no full release with a Windows installer yet. See https://cedarlogic.netlify.app for the test builds."
	}
	$relTag = [string]$release.tag_name
	$name = [string]$asset.name
	$size = [long]$asset.size
	Say "CedarLogic $($relTag.TrimStart('v')): $name ($([Math]::Round($size / 1MB, 1)) MB)"

	$running = Get-Process -Name CedarLogic -ErrorAction SilentlyContinue
	if ($running -and -not $DryRun) { Fail 'CedarLogic is open. Save your work, quit CedarLogic, then run this again.' }

	$dir = Join-Path ([IO.Path]::GetTempPath()) ('cedarlogic-install-' + [Guid]::NewGuid().ToString('N'))
	New-Item -ItemType Directory -Path $dir | Out-Null
	try {
		$file = Join-Path $dir $name
		try {
			Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $file -UseBasicParsing -UserAgent $ua
		} catch { Fail "the download failed ($($_.Exception.Message)). Try again." }
		$gotSize = (Get-Item -LiteralPath $file).Length
		$gotSha = (Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()
		if ($gotSize -ne $size) { Fail "the download is $gotSize bytes, but the release's file is $size. Try again." }

		# GitHub's own record of the file.
		$checked = @()
		$digest = $null
		if ($asset.PSObject.Properties['digest']) { $digest = [string]$asset.digest }
		if ($digest) {
			if ($digest -ne "sha256:$gotSha") { Fail "the download's SHA-256 ($gotSha) isn't the one GitHub has for the release's file ($digest)." }
			$checked += "GitHub's SHA-256"
		}

		# The signed list, when the release has one.
		$sums = $release.assets | Where-Object { $_.name -eq 'SHA256SUMS' } | Select-Object -First 1
		$sig = $release.assets | Where-Object { $_.name -eq 'SHA256SUMS.sig' } | Select-Object -First 1
		if ($sums -or $sig) {
			if (-not ($sums -and $sig)) { Fail 'the release has half of its signed list (SHA256SUMS without SHA256SUMS.sig, or the other way round).' }
			$sumsFile = Join-Path $dir 'SHA256SUMS'; $sigFile = Join-Path $dir 'SHA256SUMS.sig'
			try {
				Invoke-WebRequest -Uri $sums.browser_download_url -OutFile $sumsFile -UseBasicParsing -UserAgent $ua
				Invoke-WebRequest -Uri $sig.browser_download_url -OutFile $sigFile -UseBasicParsing -UserAgent $ua
			} catch { Fail "couldn't download the release's signed list ($($_.Exception.Message)). Try again." }
			$listBytes = [IO.File]::ReadAllBytes($sumsFile)
			$sigBytes = [IO.File]::ReadAllBytes($sigFile)
			Say 'Checking the signature...'
			if (-not (Test-Ed25519 $listBytes $sigBytes (HexBytes $PublicKeyHex))) { Fail "the release's signed list doesn't check out against CedarLogic's key. Nothing was changed." }
			$lines = [Text.Encoding]::UTF8.GetString($listBytes).Split("`n")
			if ($lines.Count -lt 5 -or $lines[0] -ne $Header -or $lines[1] -ne "tag $relTag") { Fail "the signed list isn't for release $relTag." }
			$entry = @($lines | Where-Object { $_ -match '^file (\S+) ([1-9][0-9]*) ([0-9a-f]{64})$' -and $Matches[1] -eq $name })
			if ($entry.Count -ne 1) { Fail "the signed list doesn't name $name." }
			$null = $entry[0] -match '^file (\S+) ([1-9][0-9]*) ([0-9a-f]{64})$'
			if ([long]$Matches[2] -ne $gotSize -or $Matches[3] -ne $gotSha) { Fail "the download isn't the file the signed list names. Nothing was changed." }
			$checked += 'the signed list'
		}
		if ($checked.Count -eq 0) { Fail "the release gives no checksum to check the download against, so it wasn't run. Download it from https://cedarlogic.netlify.app instead." }
		Say ("Checked: " + ($checked -join ' and ') + " (SHA-256 $gotSha).")

		if ($DryRun) { Say "Dry run: $name is downloaded and checked; not installed."; return }

		$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
		if ($isAdmin) { Say 'Note: this PowerShell is running as administrator. CedarLogic installs for this account only, so run it from a normal PowerShell for your own account.' }

		Say 'Installing...'
		$log = Join-Path $dir 'setup.log'
		$proc = Start-Process -FilePath $file -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/LOG=`"$log`"" -Wait -PassThru
		if ($proc.ExitCode -ne 0) {
			$tail = if (Test-Path -LiteralPath $log) { (Get-Content -LiteralPath $log -Tail 15) -join "`n" } else { '(no log)' }
			$what = switch ($proc.ExitCode) {
				2 { 'it was cancelled' }
				5 { 'it was stopped (is CedarLogic still open?)' }
				default { "setup ended with code $($proc.ExitCode)" }
			}
			Fail "$what. The end of setup's log:`n$tail"
		}
		$installDir = $null
		try { $installDir = (Get-ItemProperty 'HKCU:\Software\CedarLogic\Native' -ErrorAction Stop).InstallDir } catch { }
		if (-not $installDir) { $installDir = Join-Path $env:LOCALAPPDATA 'Programs\CedarLogic' }
		$exe = Join-Path $installDir 'CedarLogic.exe'
		if (-not (Test-Path -LiteralPath $exe)) { Fail "setup finished, but there's no $exe." }
		Say "Installed CedarLogic $($relTag.TrimStart('v')) in $installDir. It's in the Start menu, and it keeps itself up to date."
		if (-not $NoLaunch) { Start-Process -FilePath $exe }
	} finally {
		Remove-Item -LiteralPath $dir -Recurse -Force -ErrorAction SilentlyContinue
	}
}

try {
	Install-CedarLogicFromRelease -DryRun:$DryRun -Version $Version -Arch $Arch -NoLaunch:$NoLaunch -SelfTest:$SelfTest -Tag $Tag -Repo $Repo
} catch {
	# No exit: under "irm | iex" that would close the user's PowerShell window.
	Write-Host $_.Exception.Message -ForegroundColor Red
}
