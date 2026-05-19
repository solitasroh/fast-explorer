# Release process

This document covers two flows:

1. **One-time setup** — generate the Ed25519 signing key and configure
   GitHub Pages + repository secrets. Done once per project lifetime.
2. **Per-release flow** — what you do to ship a new version.

---

## 1. One-time setup

### 1a. Generate the Ed25519 keypair

WinSparkle 0.9+ uses Ed25519 to verify auto-update payloads. The private
key signs each release ZIP; the public key is embedded in the app at
build time.

```pwsh
# from the repo root, with Python 3 + pip
pip install pynacl
python scripts/release-appcast.py keygen
```

This writes two files into the current directory:

| File                | Contents                       | Goes where?                                   |
|---------------------|--------------------------------|-----------------------------------------------|
| `ed25519_priv.b64`  | base64-encoded 32-byte private key | **GitHub Actions secret** `ED25519_PRIVATE_KEY` |
| `ed25519_pub.b64`   | base64-encoded 32-byte public key  | CMake cache var `FE_EDDSA_PUBLIC_KEY` (committed) |

> **Important** — delete `ed25519_priv.b64` from disk after copying it
> into the GitHub Secret. The private key must never enter the repo or
> any shared backup. If it leaks, generate a new pair and re-sign all
> live appcast entries.

### 1b. Add the private key as a GitHub Secret

`Settings → Secrets and variables → Actions → New repository secret`

- Name: `ED25519_PRIVATE_KEY`
- Value: contents of `ed25519_priv.b64` (a single base64 line)

If this secret is unset, the release workflow still publishes the GitHub
Release ZIP, but skips appcast publishing — clients fall back to manual
download.

### 1c. Embed the public key in the build

Edit [CMakeLists.txt](../CMakeLists.txt):

```cmake
set(FE_EDDSA_PUBLIC_KEY "<paste contents of ed25519_pub.b64>" CACHE STRING ...)
```

(or pass `-DFE_EDDSA_PUBLIC_KEY=<key>` on the command line at configure
time and bake it into the cache).

Commit the change. Every subsequent build will verify appcast signatures
against this key.

### 1d. Enable GitHub Pages for the appcast

`Settings → Pages → Build and deployment → Source: Deploy from a branch`

- Branch: `gh-pages`
- Folder: `/ (root)`

After the first successful release, the appcast will be live at
`https://<user>.github.io/<repo>/appcast.xml`.

If your username/repo differs from `solitasroh/fast-explorer`, also
update `FE_APPCAST_URL` in CMakeLists.txt to match.

---

## 2. Per-release flow

```pwsh
# 1. Bump the version (single source of truth)
#    edit CMakeLists.txt: project(FastExplorer VERSION X.Y.Z ...)
git add CMakeLists.txt
git commit -m "release: vX.Y.Z"

# 2. Tag and push
git tag vX.Y.Z
git push origin main --tags
```

The push triggers `.github/workflows/release.yml`, which will:

1. Build the Release binary on `windows-2022`.
2. Run unit tests (`ctest`).
3. Verify the embedded VERSIONINFO matches the tag — fails loudly if you
   forgot to bump `CMakeLists.txt`.
4. Run CPack to build `FastExplorer-X.Y.Z-win64.zip`.
5. Create a GitHub Release for the tag and attach the ZIP.
6. Sign the ZIP with Ed25519, append a new `<item>` to
   `appcast/appcast.xml`, commit it back to `main`, and publish the
   `appcast/` directory to the `gh-pages` branch.

Clients that have launched the app within the past 24 hours will see the
"new version available" dialog on next launch.

### Verifying a release

After the workflow finishes:

```pwsh
# the appcast should advertise the new version
curl https://<user>.github.io/<repo>/appcast.xml

# the GitHub Release should have the ZIP attached
gh release view vX.Y.Z
```

### Rolling back

GitHub Releases are immutable in spirit — don't delete a published
release. To pull an update from the appcast:

```pwsh
# locally edit appcast/appcast.xml, remove the bad <item>
git rm-and-replace ...
git commit -m "appcast: yank vX.Y.Z"
git push origin main
# then re-publish the appcast to gh-pages (push an empty commit on main
# and re-run release workflow with workflow_dispatch on the previous tag)
```

For broken auto-update payloads, the app still works at the previously
installed version — users just won't be prompted to upgrade until you
ship a fixed `X.Y.(Z+1)`.

---

## Troubleshooting

**Workflow step "Verify embedded VERSIONINFO matches tag" fails.**
You tagged `vX.Y.Z` without bumping `project(VERSION ...)` in
CMakeLists.txt. Delete the tag, fix the version, recommit, re-tag, push.

**Auto-update dialog never appears for users.**
Check `HKCU\Software\Fast Explorer Project\Fast Explorer\WinSparkle`:
`CheckForUpdates` should be `1` and `LastCheckTime` should be a recent
Unix epoch. WinSparkle skips if the last check was less than 24 hours
ago — `win_sparkle_set_update_check_interval` can shorten this for
testing.

**"Update is improperly signed" error in the WinSparkle dialog.**
The `ED25519_PRIVATE_KEY` secret signed the ZIP with a key that doesn't
match the public key embedded in the running app. Either the build's
`FE_EDDSA_PUBLIC_KEY` is stale, or the secret was rotated without
updating it. Re-run setup steps 1a–1c.
