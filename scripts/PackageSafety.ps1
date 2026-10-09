function Get-ForestPortableFullPath([string]$Path, [string]$BasePath) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "Path must not be empty."
    }
    if ($Path -match "^[A-Za-z]:[^\\/]") {
        throw "Drive-relative paths are not supported: $Path"
    }
    if (-not [IO.Path]::IsPathRooted($Path)) {
        if ([string]::IsNullOrWhiteSpace($BasePath)) {
            throw "A base path is required for relative paths."
        }
        $Path = Join-Path $BasePath $Path
    }
    return [IO.Path]::GetFullPath($Path)
}

function Test-ForestPortablePathWithin([string]$Path, [string]$ParentPath) {
    $fullPath = Get-ForestPortableFullPath $Path ""
    $fullParent = Get-ForestPortableFullPath $ParentPath ""
    $root = [IO.Path]::GetPathRoot($fullParent)
    $prefix = $fullParent
    if ($prefix.Length -gt $root.Length) {
        $prefix = $prefix.TrimEnd([char[]]@('\', '/'))
    }
    if ($prefix[$prefix.Length - 1] -ne [IO.Path]::DirectorySeparatorChar) {
        $prefix += [IO.Path]::DirectorySeparatorChar
    }
    return $fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
}

function Test-ForestPortablePathsOverlap([string]$PathA, [string]$PathB) {
    $fullA = Get-ForestPortableFullPath $PathA ""
    $fullB = Get-ForestPortableFullPath $PathB ""
    return [String]::Equals($fullA, $fullB, [StringComparison]::OrdinalIgnoreCase) -or
        (Test-ForestPortablePathWithin $fullA $fullB) -or
        (Test-ForestPortablePathWithin $fullB $fullA)
}

function Assert-ForestNoReparsePoints([string]$Path) {
    $fullPath = Get-ForestPortableFullPath $Path ""
    $root = [IO.Path]::GetPathRoot($fullPath)
    if (-not $root) {
        throw "Path has no filesystem root: $Path"
    }

    $current = $root
    foreach ($segment in ($fullPath.Substring($root.Length) -split '[\\/]')) {
        if (-not $segment) {
            continue
        }
        $current = Join-Path $current $segment
        $item = Get-Item -Force -LiteralPath $current -ErrorAction SilentlyContinue
        if ($item) {
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Reparse points are not allowed in package paths: $current"
            }
        }
    }
}

function Assert-ForestPortableOutputSafe([string]$RepoRoot, [string]$BuildDir, [string]$OutputDir) {
    $repo = Get-ForestPortableFullPath $RepoRoot ""
    $build = Get-ForestPortableFullPath $BuildDir $repo
    $source = Join-Path $repo "src"
    $artifactRoot = Join-Path $repo "artifacts"
    $output = Get-ForestPortableFullPath $OutputDir $repo
    $outputParent = Split-Path -Parent $output
    $zip = "$output.zip"

    if (-not [String]::Equals($outputParent, $artifactRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "OutputDir must be one dedicated child of the repository artifacts directory: $output"
    }
    if ((Test-ForestPortablePathsOverlap $output $build) -or
        (Test-ForestPortablePathsOverlap $zip $build) -or
        (Test-ForestPortablePathsOverlap $output $source) -or
        (Test-ForestPortablePathsOverlap $zip $source)) {
        throw "OutputDir or its ZIP path overlaps the source or build directory: $output"
    }

    foreach ($path in @($repo, $artifactRoot, $source, $build, $output, $zip)) {
        Assert-ForestNoReparsePoints $path
    }
    if (Test-Path -LiteralPath $output) {
        throw "Output already exists and is not proven to contain only disposable package files. Any app_data, backup, or unknown content is preserved: $output"
    }
    if (Test-Path -LiteralPath $zip) {
        throw "ZIP output already exists and will not be overwritten: $zip"
    }

    return [pscustomobject]@{
        RepoRoot = $repo
        BuildPath = $build
        ArtifactRoot = $artifactRoot
        OutputDirectory = $output
        ZipPath = $zip
    }
}
