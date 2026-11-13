param([string]$Manifest,[string]$Fixture)
$ErrorActionPreference="Stop"
Import-Module $Manifest
Test-ModuleManifest $Manifest|Out-Null
$c=Connect-TitanFileFilterSDK
try{$rejected=$false;try{Invoke-TitanFileFilterSDK -Client $c -Operation check|Out-Null}catch{$rejected=$true};if(-not $rejected){throw "Invalid request accepted"};if($Fixture){$tests=Get-Content -Raw -LiteralPath 'D:\GithubRepo\Titan-File-Filter\Titan-File-Filter-PowerShell-gallery\docs\contract.json'|ConvertFrom-Json -AsHashtable;foreach($test in $tests){$result=Invoke-TitanFileFilterSDK -Client $c -Operation $test.operation -Arguments ([hashtable]$test.arguments);$value=$result.($test.expected_key);if($test.ContainsKey("expected_value") -and $value -cne $test.expected_value){throw "Wrong result"};if($test.ContainsKey("minimum_length") -and @($value).Count -lt $test.minimum_length){throw "Array missing"}}}}finally{Disconnect-TitanFileFilterSDK -Client $c}
