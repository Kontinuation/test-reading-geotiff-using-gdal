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
    fprintf(stderr, "Usage: %s <path> <iterations> <seed> <xmin,ymin,xmax,ymax> <mode>\n", program_name);
    fprintf(stderr, "\nModes:\n");
    fprintf(stderr, "  direct              - Read directly from GeoTIFF, create new dataset each iteration\n");
    fprintf(stderr, "  direct_reuse_ds     - Read directly from GeoTIFF, reuse same dataset\n");
    fprintf(stderr, "  direct_reuse_band   - Read directly from GeoTIFF, reuse same raster band\n");
    fprintf(stderr, "  vrt_api             - Read from VRT dataset created using VRT API\n");
    fprintf(stderr, "  vrt_xml             - Read from VRT dataset created from XML\n");
    fprintf(stderr, "  vrt_api_reuse_source - VRT API mode but reuse same source\n");
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
    GDALGetGeoTransform(dataset, adfGeoTransform);
    
    // Inverse transform from geo to pixel
    double denom = adfGeoTransform[1] * adfGeoTransform[5] - adfGeoTransform[2] * adfGeoTransform[4];
    *pixel_x = (int)((adfGeoTransform[5] * (geo_x - adfGeoTransform[0]) - adfGeoTransform[2] * (geo_y - adfGeoTransform[3])) / denom);
    *pixel_y = (int)((-adfGeoTransform[4] * (geo_x - adfGeoTransform[0]) + adfGeoTransform[1] * (geo_y - adfGeoTransform[3])) / denom);
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

GDALDatasetH create_vrt_from_xml(const char *source_path, GDALDatasetH source_ds, BoundingBox *bbox, GDALDatasetH *reused_source) {
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
    
    // Create VRT XML and open it
    char vrt_xml[VRT_XML_BUFFER_SIZE];
    const char *datatype_name = GDALGetDataTypeName(datatype);
    
    snprintf(vrt_xml, VRT_XML_BUFFER_SIZE,
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
    
    GDALDatasetH vrt_ds = GDALOpen(vrt_xml, GA_ReadOnly);
    
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
    if (argc != 6) {
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
                GDALDatasetH vrt_ds = create_vrt_from_xml(path, source_ds, &bbox, NULL);
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
                GDALDatasetH vrt_ds = create_vrt_from_xml(path, reused_vrt_source, &bbox, &reused_vrt_source);
                pixel_value = read_pixel_from_dataset(vrt_ds, random_x, random_y);
                GDALClose(vrt_ds);
                break;
            }
        }
        
        // Optionally print pixel value (disabled by default)
        // printf("Iteration %d: pixel value at (%.2f, %.2f) = %.2f\n", i + 1, random_x, random_y, pixel_value);
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
