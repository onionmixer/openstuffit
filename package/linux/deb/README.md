# Debian Packaging Output

`make deb` writes binary package artifacts here.

Expected primary packages:

- `openstuffit_*.deb`
- `libopenstuffit0_*.deb`
- `libopenstuffit-dev_*.deb`

Additional `*.changes`, `*.buildinfo`, and `*-dbgsym_*.ddeb` files can also
appear depending on the build environment.

File Roller integration artifacts are intentionally excluded from Debian package
payloads.
