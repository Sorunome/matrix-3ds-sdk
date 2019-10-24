[![Support room on Matrix](https://img.shields.io/matrix/matrix-3ds-sdk:sorunome.de.svg?label=%23matrix-3ds-sdk:sorunome.de&logo=matrix&server_fqdn=sorunome.de)](https://matrix.to/#/#matrix-3ds-sdk:sorunome.de) [![donate](https://liberapay.com/assets/widgets/donate.svg)](https://liberapay.com/Sorunome/donate)

# matrix-3ds-sdk

This is a matrix SDK for the Nintendo 3DS.

## Compilation

```bash
make
make install
```

## Usage

This library depends on jansson and libcurl, so be sure to add the following libraries to your projects makefile:
```
-lmatrix-3ds-sdk -ljansson `curl-config --libs`
```
