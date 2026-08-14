[CmdletBinding()]
param(
	[Parameter(Mandatory)]
	[string]$PatchPath,

	[switch]$CheckOnly
)

$ErrorActionPreference = 'Stop'
$resolvedPatchPath = (Resolve-Path -LiteralPath $PatchPath).Path
$patchBytes = [System.IO.File]::ReadAllBytes($resolvedPatchPath)

try
{
	# Throws for UTF-16/legacy-code-page patches while permitting a UTF-8 BOM.
	$strictUtf8 = [System.Text.UTF8Encoding]::new($false, $true)
	$null = $strictUtf8.GetString($patchBytes)
}
catch [System.Text.DecoderFallbackException]
{
	throw "Patch must be UTF-8. Re-save '$resolvedPatchPath' as UTF-8 (preferably without BOM)."
}

& git apply --check -- $resolvedPatchPath
if ($LASTEXITCODE -ne 0)
{
	throw 'Patch validation failed; no file was changed.'
}

if (-not $CheckOnly)
{
	& git apply -- $resolvedPatchPath
	if ($LASTEXITCODE -ne 0)
	{
		throw 'Patch application failed after validation.'
	}
}
