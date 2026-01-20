# GDAL Testing

This is a simple CLI program written in C to test randomly reading pixel data from GDAL datasets. The purpose is to test the performace of

* Reading GeoTIFF from local disk or S3 (using /vsis3)
* Decode GeoTIFF tiles
* Construct VRT dataset referencing only a tile of the underlying GeoTIFF dataset using VRT API
* Generating VRT XML in memory and opening it using GDALOpen
* Impact of various caches, including the block cache and VSICURL cache.

## Requirements

This CLI program takes the following arguments:

- Path to a GeoTIFF dataset (local path or /vsis3 path)
- Number of iterations to run
- Seed for the random number generator
- Four numbers in xmin,ymin,xmax,ymax format specifying the bounding box to read pixels from
- Mode:
    * "direct" to read directly from the GeoTIFF dataset,
    * "direct_reuse_ds" to read directly from the GeoTIFF dataset but reusing the same GDALDataset object for all iterations,
    * "direct_reuse_band" to read directly from the GeoTIFF dataset but reusing the same GDALRasterBand object for all iterations,
    * "vrt_api" to read from a VRT dataset referencing a tile of the GeoTIFF dataset. The VRT dataset is constructed using the VRT API (VRTCreate, VRTAddSimpleSource, etc.).
    * "vrt_xml" to read from a VRT dataset constructed by generating VRT XML in memory and opening it using GDALOpen.
    * "vrt_api_reuse_source": similar to "vrt_api", but reusing the same VRT source object for all iterations.

The VRT created only references a portion of the underlying GeoTIFF dataset. The portion is determined based on the xmin,ymin,xmax,ymax bounding box provided. This is to simulate the case where the GeoTIFF dataset is very large, and we only want to read a small portion of it.

What does each iteration do:

1. Generate a random world coordinate position in the specified bounding box
2. Create or reuse the GDAL dataset or raster band or VRT dataset according to the mode
3. Read the pixel value at the random position using GDALRasterIO with `GF_Read` and `GDT_Float32` data type.
4. Optionally print the pixel value read (disabled by default to avoid cluttering the output)

direct mode will create a new GDALDataset in each iteration, and close it after reading.

vrt_api, vrt_xml, vrt_api_reuse_source modes will create a VRT dataset in each iteration, and close it after reading.

vrt_api_reuse_source mode will create a VRT dataset in each iteration, but reuse the same VRT source object for all iterations.

## Building

To build the program, you need to have GDAL installed on your system. I prefer to simply write a Makefile for this simple C program.

You can use a GeoTIFF file on https://wherobots-kristin-tokyo.s3.ap-northeast-1.amazonaws.com/shared/ppp_2020_1km_Aggregated.tif for testing. You can download it locally or read it directly from S3 using /vsis3 path.

You may need to install GDAL with S3 support. On Ubuntu, you can install the `gdal-bin` and `libgdal-dev` packages.
