$projectFile = "Keil_Projects\Am32G431.uvprojx"
$outputFile = "Keil_Projects\Am32G431_updated.uvprojx"
$content = Get-Content $projectFile

# Create an empty output file
Set-Content -Path $outputFile -Value ''

# Process each line
foreach ($line in $content) {
    # Replace compiler version specifications
    if ($line -match '<pArmCC>' -or $line -match '<pCCUsed>') {
        $line = $line -replace '6190000::V6.19::ARMCLANG', '6230000::V6.23::ARMCLANG'
    }
    
    # Add the line to the output file
    Add-Content -Path $outputFile -Value $line
}

# Backup the original file
Copy-Item -Path $projectFile -Destination "$projectFile.backup"

# Replace the original file with the updated one
Copy-Item -Path $outputFile -Destination $projectFile -Force
Remove-Item -Path $outputFile

Write-Host "Updated compiler version to V6.23 in $projectFile (original backed up to $projectFile.backup)" 