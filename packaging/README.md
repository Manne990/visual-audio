# Packaging

`VisualAudio.info` is a generated classic Workbench Tool icon for `Visual Audio`.
The source image is `logo.png`.

Regenerate it with:

```sh
make icon
```

The icon must remain a `WBTOOL` icon with normal gadget activation
(`GACT_RELVERIFY | GACT_IMMEDIATE`). If it is replaced by a fake/default icon
or an icon with CLI-style metadata, Workbench can show the "Execute a File"
argument requester instead of launching the program directly.
