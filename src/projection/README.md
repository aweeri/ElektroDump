# Projection (M7)

Reprojects full-disk geostationary products to Web Mercator or equirectangular
output.

| File | Responsibility |
|---|---|
| `projection.h` / `.cpp` | `GeoTransform`, `Projector`, navigation → transform, source bounds |
| `webmercator.h` / `.cpp` | Web Mercator forward/inverse (EPSG:3857) |
| `geos.h` / `.cpp` | Geostationary forward/inverse (`h=35786023`, `r_eq=6378137`, `r_pol=6356752.31414`) |
| `equirect.h` / `.cpp` | Equirectangular forward/inverse |
| `reproject.h` / `.cpp` | Inverse-map resampling (bilinear colour, nearest alpha) |

The source pixel is derived from the xRIT `ImageNavigationRecord`; when a
channel has no navigation record, `default_geotransform` assumes the full image
spans the Earth's angular diameter. The output extent is the lon/lat bounding
box of the source, computed by inverting the scan angles at the image edges.
