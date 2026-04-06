param ( 
    [string]$Config = ".\debug.json",
    [Parameter(DontShow)]
    $defaults = (Get-Content $Config | ConvertFrom-Json),
    [switch]$DebugBuild = $defaults.DebugBuild,
    [switch]$Tiny = $defaults.Tiny,
    [switch]$Sound = $defaults.Sound,
    [switch]$Fullscreen = $defaults.Fullscreen,
    [int]$XRes = $defaults.XRes,
    [int]$YRes = $defaults.YRes,
    [string]$OutName = $defaults.OutName,

    [switch]$NoExe,
    [switch]$Clean,
    [switch]$Disasm,
    [switch]$SuffixWithRes,
    [int]$CrinklerTries = 0,

    [switch]$Capture,
    [switch]$VideoOnly,
    [switch]$SoundOnly
)

if ($MyInvocation.BoundParameters['defaults']) {
    throw "The -defaults parameter is not allowed. Use -Config to specify the configuration file."
}


$sourceDir = 'src' # Source files directory
$buildDir = 'obj' # Output directory of object files
$cacheDir = 'cache' # Output directory for compilation cache
$disasmDir = 'dis' # Output directory of disasembled files
$shadersDir = "$sourceDir/shaders"
$shadersIncludeFile = "$sourceDir/shaders.inl"

$infoColor = "Cyan"

# Clean previous build files
if ($Clean) {
    Write-Host "Cleaning build files" -ForegroundColor $infoColor
    Remove-Item .\*.exe, .\*.pdb, .\*.ilk
    if (Test-Path $buildDir) {
        Remove-Item $buildDir -Recurse
    }
    if (Test-Path $cacheDir) {
        Remove-Item $cacheDir -Recurse
    }
    if (Test-Path $shadersIncludeFile) {
        Remove-Item $shadersIncludeFile
    }
    return
}

# Check if MSVC build tools are accessible
try {
    Get-Command "cl" -ErrorAction Stop | Out-Null
    Get-Command "link" -ErrorAction Stop | Out-Null
} catch {
    Write-Error "MSVC Build Tools cl.exe or link.exe not found."
    return
}
# Check if Shader Minifirer is available
try {
    Get-Command "shader_minifier" -ErrorAction Stop | Out-Null
} catch {
    Write-Error "shader_minifier.exe not found."
    return
}

# Option selection logic
$HasVideo = $false
$HasSound = $false
if($Capture) { # Capture build
    if (-not ($VideoOnly -or $SoundOnly)) {
        $HasVideo = $true
        $HasSound = $true
    } elseif ($VideoOnly) {
        $HasVideo = $true
    } elseif ($SoundOnly) {
        $HasSound = $true
    }
    $Tiny = $false
    $DebugBuild = $false
    $Fullscreen = $false
}
elseif($Tiny) { # Tiny build (uses crinkler)
    $DebugBuild = $false
    $HasVideo = $true
    $HasSound = $Sound
}
elseif($DebugBuild) { # Debug build
    $HasVideo = $true
    $HasSound = $Sound
    $Tiny = $false
}
else { # Release build
    $HasVideo = $true
    $HasSound = $Sound
}

# Print option summary
Write-Host "Debug:      $DebugBuild"
Write-Host "Tiny:       $Tiny"
Write-Host "Fullscreen: $Fullscreen"
Write-Host "XRes:       $XRes"
Write-Host "YRes:       $YRes"
Write-Host "HasSound:   $HasSound"
Write-Host "HasVideo:   $HasVideo"
Write-Host ""

# Utility functions to test if a given file needs to be updated based
# on its dependencies last update
function ItemNeedsUpdate($itemPath, $dependsPaths) {
    if(-not (Test-Path -Path $itemPath)) {
        return $true
    }
    $item = Get-Item $itemPath
    foreach($dependsPath in $dependsPaths) {
        $depend = Get-Item $dependsPath
        if($depend.LastWriteTime -gt $item.LastWriteTime) {
            return $true
        }
    }
    return $false
}

# Get path of target object file for a given source file
function GetSrcObjPath($sourceFile) {
    "$buildDir/$((Get-Item $sourceFile).BaseName).obj"
}

# Get path of file to store dependencies of an object file
function GetObjDepPath($sourceFile) {
    "$cacheDir/$((Get-Item $sourceFile).BaseName).d"
}

# Generate the minified shader source, since this operation can
# take some time we only generate the file if it has been changed
$shaderFiles = Get-ChildItem -Path $shadersDir -Recurse `
                | Where-Object{$_.Extension -match '^.(frag|vert|glsl|comp)$'} `
                | ForEach-Object {$_.FullName}
if((ItemNeedsUpdate $shadersIncludeFile $shaderFiles)) {
    Write-Host "Minifying shaders..." -ForegroundColor $infoColor
    shader_minifier $shaderFiles -o $shadersIncludeFile # --no-renaming
    if($LASTEXITCODE -ne 0) {
        return;
    }
}

# Available options:
# https://learn.microsoft.com/en-us/cpp/build/reference/compiler-options-listed-by-category?view=msvc-170
$compileOptions = @(
    '/c', # Compile without linking (generate object files only)
    '/arch:IA32', # Force to use x87 float instructions
    '/O1', # Optimize for small code
    '/Os', # Favor small code
    '/Oi', # Replace function calls with intrinsic when possible
    '/fp:fast' # Allow reordering of float operations
)

if($Capture) {
    $compileOptions += '/DCAPTURE'
}
elseif($DebugBuild) {
    $compileOptions += '/DDEBUG'
    $compileOptions += '/Zi' # Generate debugging information
}
else { # Release
    $compileOptions += '/GS-' # No buffer security check
}
if($HasVideo) {
    $compileOptions += '/DVIDEO'
}
if ($HasSound) {
    $compileOptions += '/DSOUND'
}
if($Fullscreen) {
    $compileOptions += '/DFULLSCREEN'
}
$compileOptions += "/DXRES=$XRes"
$compileOptions += "/DYRES=$YRes"


# Get all source files
$sourceFiles = Get-ChildItem -Path $sourceDir -Filter "*.c" -Recurse `
                | ForEach-Object {$_.FullName}

# Create build directory if not already
if (-not (Test-Path -Path $buildDir)) {
    mkdir $buildDir | Out-Null
}
if (-not (Test-Path -Path $cacheDir)) {
    mkdir $cacheDir | Out-Null
}


# Compile
# Basic incremental build implementation

function IsSubPath($parent, $child) {
    $parentPath = (Resolve-Path -Path $parent).Path.TrimEnd('\', '/')
    $childPath = (Resolve-Path -Path $child).Path.TrimEnd('\', '/')

    $childPath.StartsWith(
        $parentPath + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase) -or
    ($childPath -eq $parentPath)
}

function GetDependenciesFromClOutput($clOutput) {
    $clOutput | ForEach-Object {
        if($_ -match '^\s*Note: including file:\s+(.*)$') { $Matches[1].Trim() }
    } | Where-Object { $_ -and (Test-Path $_) -and (IsSubPath $sourceDir $_)} `
      | Sort-Object -Unique
}


# Compile only the sources that have been modified except if compile
# options changed

# Check if any compile options have changed, recompile if changed
$prevOptsPath = "$cacheDir/build.cache"
$Recompile = $Clean
if(-not $Recompile) {
    if(-not (Test-Path -Path $prevOptsPath)) {
        $Recompile = $true
    } else {
        $prevOpts = @(Get-Content -Path $prevOptsPath)
        $cmp = Compare-Object $compileOptions $prevOpts
        $Recompile = -not $null -eq $cmp
    }
}
if($Recompile) { # Save new compile options
    $compileOptions | Set-Content -Path $prevOptsPath
}

# Gather source files that needs to be recompiled
$compileSources = @()
if(-not $Recompile) {
    # If not a full compilation, check for modified sources or dependencies
    foreach($source in $sourceFiles) {
        $objPath = GetSrcObjPath $source
        $depPath = GetObjDepPath $source
        $depsPaths = @($source) # Paths of required sources for this object file
        if(Test-Path $depPath) {
            $depsPaths += Get-Content $depPath
        } else { # No dependency cache yet
            $compileSources += $source
            continue
        }
        if(ItemNeedsUpdate $objPath $depsPaths) {
            # Compile if object file is older than any of its dependencies
            $compileSources += $source
        }
    }
} else {
    $compileSources = $sourceFiles
}
$compileSources = $compileSources | Sort-Object -Unique

# Compile source files
$hasCompiledFiles = ($compileSources.count -ne 0)
if($hasCompiledFiles) {
    Write-Host "Compile options: $compileOptions"

    foreach($source in $compileSources) {
        Write-Host "Compiling $source" -ForegroundColor $infoColor

        $objPath = GetSrcObjPath $source
        $depPath = GetObjDepPath $source

        $clOutput = cl $compileOptions "/Fo$objPath" /showIncludes $source 2>&1
        $code = $LASTEXITCODE

        $srcDeps = GetDependenciesFromClOutput $clOutput
        $srcDeps | Set-Content -Path $depPath

        if($code -ne 0) {
            Write-Error "Compilation failed for: $source"
            $clOutput | Where-Object { $_ -and ($_ -notmatch '^\s*Note: including file:\s+') } |
                ForEach-Object { Write-Host $_ }
            return
        }
    }
} else {
    Write-Host "Up to date. Nothing to compile." -ForegroundColor $infoColor
}

$objectFiles = Get-ChildItem -Path $buildDir -Filter "*.obj" -Recurse `
                | ForEach-Object {$_.FullName}

if(-not $NoExe) {
    if($SuffixWithRes) {
        $outFile = "$OutName_$YRes.exe"
    } else {
        $outFile = "$OutName.exe"
    }

    if($Capture) {
        $Tiny = $false # No compression when in capture mode
        $outFile = "capture_$outFile"
    }

    # Link
    if ($Tiny) {
        Write-Host "Linking with crinkler" -ForegroundColor $infoColor
        # Doc: https://github.com/runestubbe/Crinkler/blob/master/doc/manual.txt

        $extraOptions = @()
        if($CrinklerTries -gt 0) {
            $extraOptions += "/ORDERTRIES:$CrinklerTries"
        }

        crinkler /OUT:$outFile `
                /SUBSYSTEM:WINDOWS `
                /ENTRY:wWinMain `
                /TINYHEADER `
                $extraOptions `
                $objectFiles `
                kernel32.lib user32.lib gdi32.lib opengl32.lib bufferoverflowu.lib Winmm.lib

        if($LASTEXITCODE -ne 0) {
            Write-Error "Linking failed."
            return
        }
    } else {
        Write-Host "Default linking" -ForegroundColor $infoColor

        $linkOptions = @()
        if ($DebugBuild) {
            $linkOptions += "/DEBUG"
        }
        if ($Capture) {
            $linkOptions += "/SUBSYSTEM:CONSOLE"
            $linkOptions += "/ENTRY:wWinMainCRTStartup"
        }
        
        $linkOutput = link /OUT:$outFile $linkOptions `
            $objectFiles `
            user32.lib gdi32.lib opengl32.lib Winmm.lib 2>&1
        
        if($LASTEXITCODE -ne 0) {
            Write-Error "Linking failed."
            $linkOutput | ForEach-Object { Write-Host $_ }
            return
        }
    }

    Write-Host "Output file: $outFile" -ForegroundColor $infoColor
}

# Optional disassembly for debugging
if($Disasm) {
    if(-not (Test-Path -Path $disasmDir)) {
        mkdir $disasmDir | Out-Null
    }
    Write-Host "Disassembling generated object files" -ForegroundColor $infoColor
    foreach($objectFile in $objectFiles) {
        $baseName = (Split-Path $objectFile -Leaf).Split('.')[0]
        $dumpbinOptions = @(
            "/OUT:$disasmDir/$baseName.asm",
            "/DISASM"
        )
        dumpbin $dumpbinOptions $objectFile | Out-Null
    }
}