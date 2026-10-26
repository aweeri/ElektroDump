# Output (M7)

Encodes products and persists them in the product store.

| File | Responsibility |
|---|---|
| `product_meta.h` | `ProductMeta` + optional `GeoReference` |
| `webp_writer.h` / `.cpp` | WebP (libwebp, optional) with PNG/PPM fallback |
| `metadata.h` / `.cpp` | JSON sidecar serialise/parse |
| `product_store.h` / `.cpp` | save/list/latest/get/prune + `state/latest.json` |

Layout:

```
<root>/products/<YYYY>/<MM>/<DD>/<satellite>/<product>/<UTC>_<product>.{webp,png,ppm}
<root>/products/<YYYY>/<MM>/<DD>/<satellite>/<product>/<UTC>_<product>.json
<root>/state/latest.json
```

All writes go to `<path>.tmp` then `rename`; stale `*.tmp` files are removed on
start. `products.output_dir` overrides `storage.root` for the offline `image`
command.
