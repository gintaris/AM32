$projectFile = "Keil_Projects\Am32G431.uvprojx"
$content = Get-Content $projectFile -Raw

# Update all instances of compiler version from V6.19 to V6.23
$updatedContent = $content -replace '<pArmCC>6190000::V6.19::ARMCLANG</pArmCC>', '<pArmCC>6230000::V6.23::ARMCLANG</pArmCC>'
$updatedContent = $updatedContent -replace '<pCCUsed>6190000::V6.19::ARMCLANG</pCCUsed>', '<pCCUsed>6230000::V6.23::ARMCLANG</pCCUsed>'

# Also check for other formats that might be used
$updatedContent = $updatedContent -replace 'V6.19', 'V6.23'
$updatedContent = $updatedContent -replace '6190000', '6230000'

# Save the modified content
$updatedContent | Set-Content $projectFile

Write-Host "Updated all compiler version references to V6.23 in $projectFile" 