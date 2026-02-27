# Web Verification Loop

Fastest way to check title/menu UI behavior in this repo is usually the web build.

## Build

```bash
gmake -f Makefile.web --no-builtin-rules --no-builtin-variables
```

If your environment already handles GNU make defaults correctly, `make -f Makefile.web` also works.

## Serve

```bash
python3 -m http.server 8080 --directory build/web
```

Open:

- `http://127.0.0.1:8080/opentyrian2000.html`

## Why This Is Useful

- Fast restart loop for menu/layout/UI tweaks.
- Browser devtools are available for shell/page-level issues.
- Good for verifying title/menu transitions without desktop fullscreen/window quirks.
