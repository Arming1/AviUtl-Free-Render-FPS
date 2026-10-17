# AviUtl 1.x package documentation sources

These are the documents shipped with the AviUtl 1.x (Win32) package:
`README.en.md`, `README.ja.md`, `README.zh-CN.md`, and
`LEGACY_AVIUTL1_SETUP.md`.

`tools/package_aviutl1_release.ps1` copies them from this folder into the
release. The three READMEs and the setup notes go to the package root and to
`payload/`, and the setup notes also go to `payload/Docs/`. The script does not
read them from `dist/`. Edit them here, not in a generated package.
