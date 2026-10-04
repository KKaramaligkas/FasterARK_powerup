# Plugin Manager

The app's source is the [PluginManager](https://github.com/KKaramaligkas/PluginManager) repository, included here as the
`app` submodule. Its README covers the app, the store format and how to build
and test it.

This folder also holds the store that installed copies download (the address is
`PM_DEFAULT_STORE` in `app/src/version.h`), so it stays in FasterARK:

- `store/store.json`: the catalog. Each release's build packages it as the app's
  built-in copy (`make -C PluginManager/app package STORE=...`), after
  `tools/release_store.py --seed` points the ARK, Plugin Manager and Flow
  entries at that release.
- `store/icons/`: the entries' icons.
- `store/packages/`: packages built from source here because their authors
  publish no usable download.

`make -C PluginManager/app/tests check SEED=$PWD/PluginManager/store/store.json`
runs the app's tests against this store; CI does the same.
