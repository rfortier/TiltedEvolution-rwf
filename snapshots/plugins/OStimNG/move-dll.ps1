param([switch] $Rel)
if($Rel){
	Write-Host Moving release dll
Copy-Item .\skse\build\release-msvc\OStim.dll .\data\SKSE\Plugins
Copy-Item .\skse\build\release-msvc\OStim.pdb .\data\SKSE\Plugins
	
} else {
	Write-Host Moving debug dll
Copy-Item .\skse\build\debug-msvc\OStim.dll .\data\SKSE\Plugins
Copy-Item .\skse\build\debug-msvc\OStim.pdb .\data\SKSE\Plugins
}