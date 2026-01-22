#include "cpl_conv.h"
#include "gdal.h"
#include "gdal_vrt.h"
#include <stdio.h>
#include <stdlib.h>

static int parse_int_arg(const char *value, int *out) {
  char *end = NULL;
  long parsed = strtol(value, &end, 10);
  if (!value || value[0] == '\0' || !end || *end != '\0') {
    return 0;
  }
  if (parsed < 0 || parsed > 2147483647L) {
    return 0;
  }
  *out = (int)parsed;
  return 1;
}

int main(int argc, char *argv[]) {
  if (argc < 2 || argc > 5) {
    fprintf(stderr,
            "Usage: %s <path> [pixel_x pixel_y] "
            "[--read-before-close|--read-after-close]\n",
            argv[0]);
    return 1;
  }

  const char *path = argv[1];
  int pixel_x = -1;
  int pixel_y = -1;
  int read_before_close = 0;
  int pixel_args_seen = 0;
  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "--read-before-close") == 0) {
      read_before_close = 1;
    } else if (strcmp(argv[i], "--read-after-close") == 0) {
      read_before_close = 0;
    } else if (pixel_args_seen == 0) {
      if (!parse_int_arg(argv[i], &pixel_x)) {
        fprintf(stderr, "Error: pixel_x must be a non-negative int\n");
        return 1;
      }
      pixel_args_seen = 1;
    } else if (pixel_args_seen == 1) {
      if (!parse_int_arg(argv[i], &pixel_y)) {
        fprintf(stderr, "Error: pixel_y must be a non-negative int\n");
        return 1;
      }
      pixel_args_seen = 2;
    } else {
      fprintf(stderr, "Error: Too many arguments\n");
      return 1;
    }
  }

  if (pixel_args_seen == 1) {
    fprintf(stderr, "Error: pixel_y is required when pixel_x is provided\n");
    return 1;
  }

  GDALAllRegister();

  GDALDatasetH source_ds = GDALOpen(path, GA_ReadOnly);
  if (!source_ds) {
    fprintf(stderr, "Error: Failed to open source dataset '%s'\n", path);
    return 1;
  }

  int raster_x = GDALGetRasterXSize(source_ds);
  int raster_y = GDALGetRasterYSize(source_ds);
  if (raster_x <= 0 || raster_y <= 0) {
    fprintf(stderr, "Error: Invalid raster size (%d x %d)\n", raster_x,
            raster_y);
    GDALClose(source_ds);
    return 1;
  }

  if (pixel_x < 0 || pixel_y < 0) {
    pixel_x = raster_x / 2;
    pixel_y = raster_y / 2;
  }

  if (pixel_x >= raster_x || pixel_y >= raster_y) {
    fprintf(stderr, "Error: pixel (%d, %d) out of bounds (0..%d, 0..%d)\n",
            pixel_x, pixel_y, raster_x - 1, raster_y - 1);
    GDALClose(source_ds);
    return 1;
  }

  GDALRasterBandH source_band = GDALGetRasterBand(source_ds, 1);
  if (!source_band) {
    fprintf(stderr, "Error: Failed to get band 1 from source dataset\n");
    GDALClose(source_ds);
    return 1;
  }

  GDALDataType datatype = GDALGetRasterDataType(source_band);

  GDALDatasetH vrt_ds = VRTCreate(raster_x, raster_y);
  if (!vrt_ds) {
    fprintf(stderr, "Error: Failed to create VRT dataset\n");
    GDALClose(source_ds);
    return 1;
  }

  double geotransform[6];
  if (GDALGetGeoTransform(source_ds, geotransform) == CE_None) {
    GDALSetGeoTransform(vrt_ds, geotransform);
  }

  GDALAddBand(vrt_ds, datatype, NULL);
  GDALRasterBandH vrt_band = GDALGetRasterBand(vrt_ds, 1);
  if (!vrt_band) {
    fprintf(stderr, "Error: Failed to get band 1 from VRT dataset\n");
    GDALClose(vrt_ds);
    GDALClose(source_ds);
    return 1;
  }

  int has_nodata = FALSE;
  double nodata = GDALGetRasterNoDataValue(source_band, &has_nodata);
  if (has_nodata) {
    GDALSetRasterNoDataValue(vrt_band, nodata);
  }

  VRTAddSimpleSource((VRTSourcedRasterBandH)vrt_band, source_band, 0, 0,
                     raster_x, raster_y, 0, 0, raster_x, raster_y, NULL,
                     VRT_NODATA_UNSET);
  VRTFlushCache(vrt_ds);

  if (read_before_close) {
    float pixel_value = 0.0f;
    CPLErr err = GDALRasterIO(vrt_band, GF_Read, pixel_x, pixel_y, 1, 1,
                              &pixel_value, 1, 1, GDT_Float32, 0, 0);
    if (err != CE_None) {
      fprintf(stderr,
              "Error: Failed to read pixel from VRT before closing source\n");
      GDALClose(vrt_ds);
      GDALClose(source_ds);
      GDALDestroyDriverManager();
      return 1;
    }
    printf("Read pixel (%d, %d) before closing source: %.6f\n", pixel_x,
           pixel_y, (double)pixel_value);

    GDALClose(vrt_ds);
    GDALClose(source_ds);
    GDALDestroyDriverManager();
    return 0;
  }

  GDALClose(source_ds);
  source_ds = NULL;

  float pixel_value = 0.0f;
  CPLErr err = GDALRasterIO(vrt_band, GF_Read, pixel_x, pixel_y, 1, 1,
                            &pixel_value, 1, 1, GDT_Float32, 0, 0);
  if (err != CE_None) {
    fprintf(stderr,
            "Error: Failed to read pixel from VRT after closing source\n");
    GDALClose(vrt_ds);
    GDALDestroyDriverManager();
    return 1;
  }

  printf("Read pixel (%d, %d) after closing source: %.6f\n", pixel_x, pixel_y,
         (double)pixel_value);

  GDALClose(vrt_ds);
  GDALDestroyDriverManager();

  return 0;
}
