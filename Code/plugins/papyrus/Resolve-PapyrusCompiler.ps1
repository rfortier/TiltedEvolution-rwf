<#
.SYNOPSIS
Resolves a verified papyrus.exe, downloading the pinned release if needed.

.DESCRIPTION
Dot-source this file and call Resolve-PapyrusCompiler. It exists so the pinned
compiler release is defined in exactly one place: two compile scripts that each
carried their own copy of the tag and the hash could be bumped one at a time,
and the one that was missed would keep building with a compiler nobody chose.

The pin is a release tag plus a SHA-256, not "latest". A compiler update is a
reviewed change: bump both constants here, run every compile script, and commit
the result. The hash is what makes the tag mean anything - a tag can be moved,
a hash cannot.

The upstream project (russo-2025/papyrus-compiler, MIT) is archived in
snapshots/tools/papyrus-compiler/ so its source survives the repository
disappearing. That archive is not used to build: it excludes the Bethesda
binaries the upstream repository commits under bin/, so it cannot produce this
executable. Building from it needs the V toolchain, which is a much larger
dependency than a download.
#>

# Pinned, not "latest": a compiler update must be a reviewed change, and the
# hash is what makes the pin mean anything.
$script:PapyrusCompilerReleaseTag = "2026.03.15"
$script:PapyrusCompilerAssetName = "papyrus-compiler-windows.zip"
$script:PapyrusCompilerAssetSha256 = "67b44c77d00a5cda986bec6af5c228a56abe6ec1fadcb4cfea5c858e6941140e"

function Resolve-PapyrusCompiler {
    [CmdletBinding()]
    param(
        # Use an already-unpacked papyrus.exe instead of downloading one.
        [string]$CompilerPath,

        # Where the pinned release is unpacked. Cached between runs.
        [string]$ToolRoot,

        # Repository root; .papyrus-tool lands here by default.
        [string]$RepoRoot = (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
    )

    if ($CompilerPath) {
        if (-not (Test-Path -LiteralPath $CompilerPath)) {
            throw "CompilerPath does not exist: $CompilerPath"
        }
        return (Resolve-Path -LiteralPath $CompilerPath).Path
    }

    if (-not $ToolRoot) { $ToolRoot = Join-Path $RepoRoot ".papyrus-tool" }
    $compiler = Join-Path $ToolRoot "papyrus-compiler/papyrus.exe"

    if (-not (Test-Path -LiteralPath $compiler)) {
        New-Item -ItemType Directory -Force -Path $ToolRoot | Out-Null
        $archive = Join-Path $ToolRoot $script:PapyrusCompilerAssetName
        $url = "https://github.com/russo-2025/papyrus-compiler/releases/download/$script:PapyrusCompilerReleaseTag/$script:PapyrusCompilerAssetName"

        if (-not (Test-Path -LiteralPath $archive)) {
            Write-Host "downloading papyrus-compiler $script:PapyrusCompilerReleaseTag"
            Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing
        }

        $actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $script:PapyrusCompilerAssetSha256) {
            Remove-Item -LiteralPath $archive -Force
            throw "papyrus-compiler archive hash mismatch: expected $script:PapyrusCompilerAssetSha256 but got $actual"
        }

        Expand-Archive -LiteralPath $archive -DestinationPath $ToolRoot -Force
        if (-not (Test-Path -LiteralPath $compiler)) {
            throw "papyrus.exe was not found in the archive at $compiler"
        }
    }

    return $compiler
}
