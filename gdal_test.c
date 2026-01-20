#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gdal.h"
#include "gdal_vrt.h"
#include "cpl_conv.h"
#include "cpl_string.h"

#define VRT_XML_BUFFER_SIZE 4096

typedef enum {
    MODE_DIRECT,
    MODE_DIRECT_REUSE_DS,
    MODE_DIRECT_REUSE_BAND,
    MODE_VRT_API,
    MODE_VRT_XML,
    MODE_VRT_API_REUSE_SOURCE,
    MODE_INVALID
} Mode;

typedef struct {
    double xmin;
    double ymin;
    double xmax;
    double ymax;
} BoundingBox;

void print_usage(const char *program_name) {
    fprintf(stderr, "Usage: %s <path> <iterations> <seed> <xmin,ymin,xmax,ymax> <mode> [--print-pixels]\n", program_name);
    fprintf(stderr, "\nModes:\n");
    fprintf(stderr, "  direct              - Read directly from GeoTIFF, create new dataset each iteration\n");
    fprintf(stderr, "  direct_reuse_ds     - Read directly from GeoTIFF, reuse same dataset\n");
    fprintf(stderr, "  direct_reuse_band   - Read directly from GeoTIFF, reuse same raster band\n");
    fprintf(stderr, "  vrt_api             - Read from VRT dataset created using VRT API\n");
    fprintf(stderr, "  vrt_xml             - Read from VRT dataset created from XML\n");
    fprintf(stderr, "  vrt_api_reuse_source - VRT API mode but reuse same source\n");
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  --print-pixels      - Print pixel value for each iteration (disabled by default)\n");
}

Mode parse_mode(const char *mode_str) {
    if (strcmp(mode_str, "direct") == 0) {
        return MODE_DIRECT;
    } else if (strcmp(mode_str, "direct_reuse_ds") == 0) {
        return MODE_DIRECT_REUSE_DS;
    } else if (strcmp(mode_str, "direct_reuse_band") == 0) {
        return MODE_DIRECT_REUSE_BAND;
    } else if (strcmp(mode_str, "vrt_api") == 0) {
        return MODE_VRT_API;
    } else if (strcmp(mode_str, "vrt_xml") == 0) {
        return MODE_VRT_XML;
    } else if (strcmp(mode_str, "vrt_api_reuse_source") == 0) {
        return MODE_VRT_API_REUSE_SOURCE;
    } else {
        return MODE_INVALID;
    }
}

int parse_bbox(const char *bbox_str, BoundingBox *bbox) {
    return sscanf(bbox_str, "%lf,%lf,%lf,%lf", &bbox->xmin, &bbox->ymin, &bbox->xmax, &bbox->ymax) == 4;
}

void geo_to_pixel(GDALDatasetH dataset, double geo_x, double geo_y, int *pixel_x, int *pixel_y) {
    double adfGeoTransform[6];
    double adfInvGeoTransform[6];
    
    // Get the geotransform (pixel -> world)
    if (GDALGetGeoTransform(dataset, adfGeoTransform) != CE_None) {
        fprintf(stderr, "Warning: Dataset has no geotransform\n");
        *pixel_x = 0;
        *pixel_y = 0;
        return;
    }
    
    // Invert the geotransform (world -> pixel) using GDAL's built-in function
    if (!GDALInvGeoTransform(adfGeoTransform, adfInvGeoTransform)) {
        fprintf(stderr, "Warning: Geotransform is not invertible\n");
        *pixel_x = 0;
        *pixel_y = 0;
        return;
    }
    
    // Apply the inverted transform to convert world coordinates to pixel coordinates
    double pixel_x_d, pixel_y_d;
    GDALApplyGeoTransform(adfInvGeoTransform, geo_x, geo_y, &pixel_x_d, &pixel_y_d);
    
    *pixel_x = (int)pixel_x_d;
    *pixel_y = (int)pixel_y_d;
}

char* create_vrt_xml(const char *source_path, GDALDatasetH source_ds, BoundingBox *bbox) {
    double adfGeoTransform[6];
    GDALGetGeoTransform(source_ds, adfGeoTransform);
    
    int xmin_pix, ymin_pix, xmax_pix, ymax_pix;
    geo_to_pixel(source_ds, bbox->xmin, bbox->ymax, &xmin_pix, &ymin_pix);
    geo_to_pixel(source_ds, bbox->xmax, bbox->ymin, &xmax_pix, &ymax_pix);
    
    int width = xmax_pix - xmin_pix;
    int height = ymax_pix - ymin_pix;
    
    if (width <= 0) width = 1;
    if (height <= 0) height = 1;
    
    // Calculate new geotransform
    double new_geo_x = adfGeoTransform[0] + xmin_pix * adfGeoTransform[1] + ymin_pix * adfGeoTransform[2];
    double new_geo_y = adfGeoTransform[3] + xmin_pix * adfGeoTransform[4] + ymin_pix * adfGeoTransform[5];
    
    GDALRasterBandH band = GDALGetRasterBand(source_ds, 1);
    GDALDataType datatype = GDALGetRasterDataType(band);
    const char *datatype_name = GDALGetDataTypeName(datatype);
    
    char *xml = (char*)malloc(VRT_XML_BUFFER_SIZE);
    snprintf(xml, VRT_XML_BUFFER_SIZE,
        "<VRTDataset rasterXSize=\"%d\" rasterYSize=\"%d\">\n"
        "  <GeoTransform>%.15f, %.15f, %.15f, %.15f, %.15f, %.15f</GeoTransform>\n"
        "  <VRTRasterBand dataType=\"%s\" band=\"1\">\n"
        "    <SimpleSource>\n"
        "      <SourceFilename relativeToVRT=\"0\">%s</SourceFilename>\n"
        "      <SourceBand>1</SourceBand>\n"
        "      <SrcRect xOff=\"%d\" yOff=\"%d\" xSize=\"%d\" ySize=\"%d\"/>\n"
        "      <DstRect xOff=\"0\" yOff=\"0\" xSize=\"%d\" ySize=\"%d\"/>\n"
        "    </SimpleSource>\n"
        "  </VRTRasterBand>\n"
        "</VRTDataset>\n",
        width, height,
        new_geo_x, adfGeoTransform[1], adfGeoTransform[2],
        new_geo_y, adfGeoTransform[4], adfGeoTransform[5],
        datatype_name,
        source_path,
        xmin_pix, ymin_pix, width, height,
        width, height);
    
    return xml;
}

GDALDatasetH create_vrt_api(const char *source_path, GDALDatasetH source_ds, BoundingBox *bbox) {
    double adfGeoTransform[6];
    GDALGetGeoTransform(source_ds, adfGeoTransform);
    
    int xmin_pix, ymin_pix, xmax_pix, ymax_pix;
    geo_to_pixel(source_ds, bbox->xmin, bbox->ymax, &xmin_pix, &ymin_pix);
    geo_to_pixel(source_ds, bbox->xmax, bbox->ymin, &xmax_pix, &ymax_pix);
    
    int width = xmax_pix - xmin_pix;
    int height = ymax_pix - ymin_pix;
    
    if (width <= 0) width = 1;
    if (height <= 0) height = 1;
    
    // Calculate new geotransform
    double new_geo_x = adfGeoTransform[0] + xmin_pix * adfGeoTransform[1] + ymin_pix * adfGeoTransform[2];
    double new_geo_y = adfGeoTransform[3] + xmin_pix * adfGeoTransform[4] + ymin_pix * adfGeoTransform[5];
    
    GDALRasterBandH source_band = GDALGetRasterBand(source_ds, 1);
    GDALDataType datatype = GDALGetRasterDataType(source_band);
    
    // Create VRT dataset using VRT API
    GDALDatasetH vrt_ds = VRTCreate(width, height);
    if (!vrt_ds) {
        return NULL;
    }
    
    // Set geotransform
    double new_transform[6] = {new_geo_x, adfGeoTransform[1], adfGeoTransform[2],
                                new_geo_y, adfGeoTransform[4], adfGeoTransform[5]};
    GDALSetGeoTransform(vrt_ds, new_transform);
    
    // Add band
    GDALAddBand(vrt_ds, datatype, NULL);
    
    // Get the VRT band
    GDALRasterBandH vrt_band = GDALGetRasterBand(vrt_ds, 1);
    
    // Add simple source
    VRTAddSimpleSource((VRTSourcedRasterBandH)vrt_band,
                       source_band,
                       xmin_pix, ymin_pix, width, height,
                       0, 0, width, height,
                       NULL, VRT_NODATA_UNSET);
    
    // Flush cache to finalize the VRT
    VRTFlushCache(vrt_ds);
    
    return vrt_ds;
}

float read_pixel_from_dataset(GDALDatasetH dataset, double geo_x, double geo_y) {
    int pixel_x, pixel_y;
    geo_to_pixel(dataset, geo_x, geo_y, &pixel_x, &pixel_y);
    
    GDALRasterBandH band = GDALGetRasterBand(dataset, 1);
    float pixel_value;
    
    CPLErr err = GDALRasterIO(band, GF_Read, pixel_x, pixel_y, 1, 1,
                              &pixel_value, 1, 1, GDT_Float32, 0, 0);
    
    if (err != CE_None) {
        fprintf(stderr, "Error reading pixel at (%d, %d)\n", pixel_x, pixel_y);
        return 0.0f;
    }
    
    return pixel_value;
}

float read_pixel_from_band(GDALRasterBandH band, GDALDatasetH dataset, double geo_x, double geo_y) {
    int pixel_x, pixel_y;
    geo_to_pixel(dataset, geo_x, geo_y, &pixel_x, &pixel_y);
    
    float pixel_value;
    
    CPLErr err = GDALRasterIO(band, GF_Read, pixel_x, pixel_y, 1, 1,
                              &pixel_value, 1, 1, GDT_Float32, 0, 0);
    
    if (err != CE_None) {
        fprintf(stderr, "Error reading pixel at (%d, %d)\n", pixel_x, pixel_y);
        return 0.0f;
    }
    
    return pixel_value;
}

int main(int argc, char *argv[]) {
    if (argc < 6 || argc > 7) {
        print_usage(argv[0]);
        return 1;
    }
    
    const char *path = argv[1];
    int iterations = atoi(argv[2]);
    unsigned int seed = (unsigned int)atoi(argv[3]);
    BoundingBox bbox;
    if (!parse_bbox(argv[4], &bbox)) {
        fprintf(stderr, "Error: Invalid bounding box format. Use xmin,ymin,xmax,ymax\n");
        return 1;
    }
    
    Mode mode = parse_mode(argv[5]);
    if (mode == MODE_INVALID) {
        fprintf(stderr, "Error: Invalid mode '%s'\n", argv[5]);
        print_usage(argv[0]);
        return 1;
    }
    
    // Check for --print-pixels option
    int print_pixels = 0;
    if (argc == 7) {
        if (strcmp(argv[6], "--print-pixels") == 0) {
            print_pixels = 1;
        } else {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[6]);
            print_usage(argv[0]);
            return 1;
        }
    }
    
    GDALAllRegister();
    
    printf("Running %d iterations in mode '%s'\n", iterations, argv[5]);
    printf("Bounding box: (%.2f, %.2f) - (%.2f, %.2f)\n", 
           bbox.xmin, bbox.ymin, bbox.xmax, bbox.ymax);
    
    srand(seed);
    
    clock_t start_time = clock();
    
    // For reuse modes
    GDALDatasetH reused_ds = NULL;
    GDALRasterBandH reused_band = NULL;
    GDALDatasetH reused_vrt_source = NULL;
    
    // Open dataset for reuse modes
    if (mode == MODE_DIRECT_REUSE_DS || mode == MODE_DIRECT_REUSE_BAND) {
        reused_ds = GDALOpen(path, GA_ReadOnly);
        if (!reused_ds) {
            fprintf(stderr, "Error: Failed to open dataset '%s'\n", path);
            return 1;
        }
        if (mode == MODE_DIRECT_REUSE_BAND) {
            reused_band = GDALGetRasterBand(reused_ds, 1);
        }
    }
    
    for (int i = 0; i < iterations; i++) {
        // Generate random coordinate
        double random_x = bbox.xmin + ((double)rand() / RAND_MAX) * (bbox.xmax - bbox.xmin);
        double random_y = bbox.ymin + ((double)rand() / RAND_MAX) * (bbox.ymax - bbox.ymin);
        
        float pixel_value = 0.0f;
        
        switch (mode) {
            case MODE_DIRECT: {
                GDALDatasetH ds = GDALOpen(path, GA_ReadOnly);
                if (!ds) {
                    fprintf(stderr, "Error: Failed to open dataset '%s'\n", path);
                    return 1;
                }
                pixel_value = read_pixel_from_dataset(ds, random_x, random_y);
                GDALClose(ds);
                break;
            }
            
            case MODE_DIRECT_REUSE_DS: {
                pixel_value = read_pixel_from_dataset(reused_ds, random_x, random_y);
                break;
            }
            
            case MODE_DIRECT_REUSE_BAND: {
                pixel_value = read_pixel_from_band(reused_band, reused_ds, random_x, random_y);
                break;
            }
            
            case MODE_VRT_API: {
                GDALDatasetH source_ds = GDALOpen(path, GA_ReadOnly);
                if (!source_ds) {
                    fprintf(stderr, "Error: Failed to open source dataset '%s'\n", path);
                    return 1;
                }
                GDALDatasetH vrt_ds = create_vrt_api(path, source_ds, &bbox);
                if (!vrt_ds) {
                    fprintf(stderr, "Error: Failed to create VRT dataset\n");
                    GDALClose(source_ds);
                    return 1;
                }
                pixel_value = read_pixel_from_dataset(vrt_ds, random_x, random_y);
                GDALClose(vrt_ds);
                GDALClose(source_ds);
                break;
            }
            
            case MODE_VRT_XML: {
                GDALDatasetH source_ds = GDALOpen(path, GA_ReadOnly);
                if (!source_ds) {
                    fprintf(stderr, "Error: Failed to open source dataset '%s'\n", path);
                    return 1;
                }
                char *vrt_xml = create_vrt_xml(path, source_ds, &bbox);
                GDALDatasetH vrt_ds = GDALOpen(vrt_xml, GA_ReadOnly);
                free(vrt_xml);
                if (!vrt_ds) {
                    fprintf(stderr, "Error: Failed to create VRT dataset\n");
                    GDALClose(source_ds);
                    return 1;
                }
                pixel_value = read_pixel_from_dataset(vrt_ds, random_x, random_y);
                GDALClose(vrt_ds);
                GDALClose(source_ds);
                break;
            }
            
            case MODE_VRT_API_REUSE_SOURCE: {
                if (!reused_vrt_source) {
                    reused_vrt_source = GDALOpen(path, GA_ReadOnly);
                    if (!reused_vrt_source) {
                        fprintf(stderr, "Error: Failed to open source dataset '%s'\n", path);
                        return 1;
                    }
                }
                GDALDatasetH vrt_ds = create_vrt_api(path, reused_vrt_source, &bbox);
                if (!vrt_ds) {
                    fprintf(stderr, "Error: Failed to create VRT dataset\n");
                    GDALClose(reused_vrt_source);
                    reused_vrt_source = NULL;
                    return 1;
                }
                pixel_value = read_pixel_from_dataset(vrt_ds, random_x, random_y);
                GDALClose(vrt_ds);
                break;
            }
            
            case MODE_INVALID:
                // This should never happen as we check for MODE_INVALID before the loop
                fprintf(stderr, "Error: Invalid mode\n");
                return 1;
        }
        
        // Optionally print pixel value
        if (print_pixels) {
            printf("Iteration %d: pixel value at (%.2f, %.2f) = %.2f\n", i + 1, random_x, random_y, pixel_value);
        } else {
            (void)pixel_value; // Suppress unused variable warning when not printing
        }
    }
    
    // Clean up reused resources
    if (reused_ds) {
        GDALClose(reused_ds);
    }
    if (reused_vrt_source) {
        GDALClose(reused_vrt_source);
    }
    
    clock_t end_time = clock();
    double elapsed_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    
    printf("Completed %d iterations in %.3f seconds (%.3f ms per iteration)\n", 
           iterations, elapsed_time, (elapsed_time * 1000.0) / iterations);
    
    GDALDestroyDriverManager();
    
    return 0;
}
