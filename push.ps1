# Publish this folder as a GitHub repository.
# Usage:  powershell -ExecutionPolicy Bypass -File .\push.ps1 [-RepoName linux-user-space-router]
param(
    [string]$RepoName = "linux-user-space-router",
    [ValidateSet("public", "private")]
    [string]$Visibility = "public",
    [string]$CommitMessage = "init: user-space router on AF_PACKET (ARP cache + cross-subnet forwarding + IP filter)"
)

$ErrorActionPreference = "Stop"
Set-Location -Path $PSScriptRoot

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Write-Host "[!] git not found."
    Write-Host "    Install it first:  winget install --id Git.Git -e --source winget"
    Write-Host "    Then reopen the terminal and run this script again."
    exit 1
}

if (-not (Test-Path ".git")) {
    git init -b main
}

git add .
git commit -m $CommitMessage

if (Get-Command gh -ErrorAction SilentlyContinue) {
    $auth = gh auth status 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "[!] gh is installed but not logged in. Run:  gh auth login"
        exit 1
    }
    gh repo create $RepoName --$Visibility --source . --remote origin --push
} else {
    Write-Host ""
    Write-Host "gh CLI not found. Finish manually:"
    Write-Host "  1) Create an empty repo named '$RepoName' at https://github.com/new (no README, no .gitignore)"
    Write-Host "  2) Run:"
    Write-Host "       git remote add origin https://github.com/<your-name>/$RepoName.git"
    Write-Host "       git branch -M main"
    Write-Host "       git push -u origin main"
}
