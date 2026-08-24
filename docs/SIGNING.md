# Windows Code Signing

## Why signing matters

Without code signing, Windows 11 with **Smart App Control (SAC)** shows a hard
"only Don't run" block on the installer. With a signed binary, users get the
standard "More info → Run anyway" path. Over time, SmartScreen reputation
accumulates and the warning fades entirely.

## SignPath Foundation (free for OSS)

[SignPath Foundation](https://signpath.org) provides **free code signing
certificates** for open-source projects. They issue a real OV certificate in the
project's name, stored on their HSM — no USB tokens, no secrets to manage.

Used by: Notepad++, Inkscape, Flameshot, GitExtensions, and hundreds more.

### Step 1: Apply

1. Go to **https://signpath.org/apply**
2. Fill in:
   - **Project URL**: `https://github.com/jtrefon/compressor-windows`
   - **License**: Apache-2.0
   - **Contact**: your name/email
3. Approval takes 1–2 weeks for established projects (you need a track record of
   releases — we have v0.1.0–v0.1.8)

### Step 2: Configure SignPath project

After approval:

1. Log in to **https://app.signpath.io**
2. Create a project named `compressor-windows`
3. Create an **artifact configuration** named `windows-installer` containing:
   - `CompressorWindows.exe`
   - `CompressorWindows-*-setup.exe`
4. Create a **signing policy** named `release-signing`
5. Under **Trusted build systems**, connect **GitHub.com** and enter the repo
   `jtrefon/compressor-windows`

### Step 3: Add secrets to GitHub

Go to **Settings → Secrets and variables → Actions**:

| Secret | Value |
|--------|-------|
| `SIGNPATH_API_TOKEN` | API token from SignPath project settings |
| `SIGNPATH_ORG_ID` | Organization ID from SignPath settings |

### Step 4: Tag a release

Push a tag like `v0.1.9`. The release workflow will:

1. Build everything (exe + installer)
2. Verify the build (E2E tests)
3. Upload artifacts to SignPath for signing
4. SignPath signs them with the OV certificate
5. Repackage the portable zip with the signed exe
6. Compute checksums over the signed artifacts
7. Publish the release

Until SignPath is configured, the pipeline ships unsigned builds as before (no
flag day, no broken releases).

## Verification

After download, verify the signature:

```powershell
signtool verify /pa /v CompressorWindows.exe
signtool verify /pa /v CompressorWindows-0.1.9-setup.exe
```

## Fallback: PFX certificate

If you have a traditional PFX code-signing certificate, the existing
`SIGNING_CERT_BASE64` + `SIGNING_CERT_PASSWORD` secrets still work. The
pipeline uses them only when SignPath is not configured.

## Temporary workaround (no signing)

If you download the unsigned build and see the SmartScreen warning:

1. Right-click the `.exe` → **Properties**
2. Check **Unblock** at the bottom → **Apply**
3. Double-click to run

Or: Settings → Privacy & Security → Windows Security → App & browser control →
Smart App Control → turn off (one-way; re-enabling needs a Windows reset).
