# test-reading-geotiff-using-gdal

Some tests to understand the performance characteristics of GDAL regular dataset and VRT.

## Building

This project requires GDAL development libraries. On Ubuntu:

```bash
sudo apt-get install gdal-bin libgdal-dev
```

To build the CLI tool:

```bash
make
```

## Usage

```bash
./gdal_test <path> <iterations> <seed> <xmin,ymin,xmax,ymax> <mode>
```

### Arguments

- **path**: Path to a GeoTIFF dataset (local path or /vsis3 path)
- **iterations**: Number of iterations to run
- **seed**: Seed for the random number generator
- **xmin,ymin,xmax,ymax**: Bounding box to read pixels from
- **mode**: One of the following:
  - `direct` - Read directly from GeoTIFF, create new dataset each iteration
  - `direct_reuse_ds` - Read directly from GeoTIFF, reuse same dataset
  - `direct_reuse_band` - Read directly from GeoTIFF, reuse same raster band
  - `vrt_api` - Read from VRT dataset created using VRT API
  - `vrt_xml` - Read from VRT dataset created from XML
  - `vrt_api_reuse_source` - VRT API mode but reuse same source

### Example

```bash
# Test with 100 iterations on a GeoTIFF file
./gdal_test /path/to/file.tif 100 42 -180,-90,180,90 direct

# Test reading from S3
./gdal_test /vsis3/bucket/file.tif 100 42 -180,-90,180,90 vrt_xml
```

For more details, see [GDAL_testing.md](GDAL_testing.md).
