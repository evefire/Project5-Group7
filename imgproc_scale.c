#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/*
 * 模块说明：BMP 缩放程序（imgproc -z）
 * 目的：读取 8/24 位无压缩 BMP，按比例缩放并保存。
 * 思路：读取 BMP 头与像素数据 -> 选择插值算法 -> 生成新图像。
 * 最小功能：支持 -z -m n|l|c input.bmp scale_percent output.bmp。
 */

#pragma pack(push,1)
typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BITMAPFILEHEADER;

typedef struct {
    uint32_t biSize;
    int32_t  biWidth;
    int32_t  biHeight;
    uint16_t biPlanes;
    uint16_t biBitCount;
    uint32_t biCompression;
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BITMAPINFOHEADER;
#pragma pack(pop)

/* BMP 图像容器：包含文件头、信息头、调色板与像素数据 */
typedef struct {
    BITMAPFILEHEADER fileHeader;
    BITMAPINFOHEADER infoHeader;
    unsigned char *palette; /* for 8-bit */
    unsigned char *data;
} BMPIMAGE;

/* 释放 BMP 相关内存 */
static void free_bmp(BMPIMAGE *img) {
    if (!img) return;
    free(img->palette);
    free(img->data);
    free(img);
}

/* 计算 BMP 每行的字节填充（4 字节对齐） */
static int row_padding(int width, int bpp) {
    int bytes_per_row = (width * bpp + 7) / 8;
    return (4 - (bytes_per_row % 4)) % 4;
}

/* 读取 BMP（仅支持 8/24 位、无压缩） */
static BMPIMAGE *read_bmp(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    BMPIMAGE *img = (BMPIMAGE *)calloc(1, sizeof(BMPIMAGE));
    if (!img) { fclose(fp); return NULL; }

    if (fread(&img->fileHeader, sizeof(BITMAPFILEHEADER), 1, fp) != 1) {
        fclose(fp); free_bmp(img); return NULL;
    }
    if (fread(&img->infoHeader, sizeof(BITMAPINFOHEADER), 1, fp) != 1) {
        fclose(fp); free_bmp(img); return NULL;
    }

    if (img->fileHeader.bfType != 0x4D42) { /* 'BM' */
        fclose(fp); free_bmp(img); return NULL;
    }

    if (img->infoHeader.biCompression != 0) {
        fclose(fp); free_bmp(img); return NULL;
    }

    int bpp = img->infoHeader.biBitCount;
    if (bpp != 24 && bpp != 8) {
        fclose(fp); free_bmp(img); return NULL;
    }

    if (bpp == 8) {
        uint32_t palette_size = img->infoHeader.biClrUsed;
        if (palette_size == 0) palette_size = 256;
        img->palette = (unsigned char *)malloc(palette_size * 4);
        if (!img->palette) { fclose(fp); free_bmp(img); return NULL; }
        if (fread(img->palette, 4, palette_size, fp) != palette_size) {
            fclose(fp); free_bmp(img); return NULL;
        }
    }

    if (fseek(fp, img->fileHeader.bfOffBits, SEEK_SET) != 0) {
        fclose(fp); free_bmp(img); return NULL;
    }

    int width = img->infoHeader.biWidth;
    int height = abs(img->infoHeader.biHeight);
    int padding = row_padding(width, bpp);
    int bytes_per_row = (width * bpp + 7) / 8;

    img->data = (unsigned char *)malloc((bytes_per_row + padding) * height);
    if (!img->data) { fclose(fp); free_bmp(img); return NULL; }

    if (fread(img->data, 1, (bytes_per_row + padding) * height, fp) != (size_t)((bytes_per_row + padding) * height)) {
        fclose(fp); free_bmp(img); return NULL;
    }

    fclose(fp);
    return img;
}

/* 写出 BMP（保持输入的位深与调色板） */
static int write_bmp(const char *path, const BMPIMAGE *img) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;

    if (fwrite(&img->fileHeader, sizeof(BITMAPFILEHEADER), 1, fp) != 1) { fclose(fp); return 0; }
    if (fwrite(&img->infoHeader, sizeof(BITMAPINFOHEADER), 1, fp) != 1) { fclose(fp); return 0; }

    if (img->infoHeader.biBitCount == 8) {
        uint32_t palette_size = img->infoHeader.biClrUsed;
        if (palette_size == 0) palette_size = 256;
        if (fwrite(img->palette, 4, palette_size, fp) != palette_size) { fclose(fp); return 0; }
    }

    int width = img->infoHeader.biWidth;
    int height = abs(img->infoHeader.biHeight);
    int bpp = img->infoHeader.biBitCount;
    int padding = row_padding(width, bpp);
    int bytes_per_row = (width * bpp + 7) / 8;

    if (fwrite(img->data, 1, (bytes_per_row + padding) * height, fp) != (size_t)((bytes_per_row + padding) * height)) { fclose(fp); return 0; }

    fclose(fp);
    return 1;
}

/* 限制数值范围，避免溢出 */
static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* 获取 8 位像素（自动裁剪边界） */
static unsigned char get_pixel_8(const BMPIMAGE *img, int x, int y) {
    int width = img->infoHeader.biWidth;
    int height = abs(img->infoHeader.biHeight);
    int bpp = img->infoHeader.biBitCount;
    int padding = row_padding(width, bpp);
    int bytes_per_row = (width * bpp + 7) / 8;

    if (x < 0) x = 0;
    if (x >= width) x = width - 1;
    if (y < 0) y = 0;
    if (y >= height) y = height - 1;

    int row = (img->infoHeader.biHeight > 0) ? (height - 1 - y) : y;
    return img->data[row * (bytes_per_row + padding) + x];
}

/* 设置 8 位像素 */
static void set_pixel_8(BMPIMAGE *img, int x, int y, unsigned char value) {
    int width = img->infoHeader.biWidth;
    int height = abs(img->infoHeader.biHeight);
    int bpp = img->infoHeader.biBitCount;
    int padding = row_padding(width, bpp);
    int bytes_per_row = (width * bpp + 7) / 8;

    int row = (img->infoHeader.biHeight > 0) ? (height - 1 - y) : y;
    img->data[row * (bytes_per_row + padding) + x] = value;
}

/* 获取 24 位像素（BGR） */
static void get_pixel_24(const BMPIMAGE *img, int x, int y, unsigned char *bgr) {
    int width = img->infoHeader.biWidth;
    int height = abs(img->infoHeader.biHeight);
    int bpp = img->infoHeader.biBitCount;
    int padding = row_padding(width, bpp);
    int bytes_per_row = (width * bpp + 7) / 8;

    if (x < 0) x = 0;
    if (x >= width) x = width - 1;
    if (y < 0) y = 0;
    if (y >= height) y = height - 1;

    int row = (img->infoHeader.biHeight > 0) ? (height - 1 - y) : y;
    unsigned char *p = img->data + row * (bytes_per_row + padding) + x * 3;
    bgr[0] = p[0];
    bgr[1] = p[1];
    bgr[2] = p[2];
}

/* 设置 24 位像素（BGR） */
static void set_pixel_24(BMPIMAGE *img, int x, int y, const unsigned char *bgr) {
    int width = img->infoHeader.biWidth;
    int height = abs(img->infoHeader.biHeight);
    int bpp = img->infoHeader.biBitCount;
    int padding = row_padding(width, bpp);
    int bytes_per_row = (width * bpp + 7) / 8;

    int row = (img->infoHeader.biHeight > 0) ? (height - 1 - y) : y;
    unsigned char *p = img->data + row * (bytes_per_row + padding) + x * 3;
    p[0] = bgr[0];
    p[1] = bgr[1];
    p[2] = bgr[2];
}

/* 三次立方插值权重函数 */
static float cubic_weight(float x) {
    x = fabsf(x);
    if (x <= 1.0f) {
        return (1.5f * x * x * x) - (2.5f * x * x) + 1.0f;
    } else if (x < 2.0f) {
        return (-0.5f * x * x * x) + (2.5f * x * x) - (4.0f * x) + 2.0f;
    }
    return 0.0f;
}

/*
 * 执行缩放：根据比例计算目标尺寸，并按 n/l/c 插值生成新像素。
 * 最小功能：输出与输入位深一致的 BMP，保持调色板。
 */
static BMPIMAGE *create_scaled(const BMPIMAGE *src, float scale, char method) {
    int src_w = src->infoHeader.biWidth;
    int src_h = abs(src->infoHeader.biHeight);
    int bpp = src->infoHeader.biBitCount;

    int dst_w = (int)(src_w * scale + 0.5f);
    int dst_h = (int)(src_h * scale + 0.5f);
    if (dst_w < 1) dst_w = 1;
    if (dst_h < 1) dst_h = 1;

    BMPIMAGE *dst = (BMPIMAGE *)calloc(1, sizeof(BMPIMAGE));
    if (!dst) return NULL;

    dst->fileHeader = src->fileHeader;
    dst->infoHeader = src->infoHeader;
    dst->infoHeader.biWidth = dst_w;
    dst->infoHeader.biHeight = src->infoHeader.biHeight > 0 ? dst_h : -dst_h;

    int padding = row_padding(dst_w, bpp);
    int bytes_per_row = (dst_w * bpp + 7) / 8;
    dst->infoHeader.biSizeImage = (bytes_per_row + padding) * dst_h;
    dst->fileHeader.bfSize = dst->fileHeader.bfOffBits + dst->infoHeader.biSizeImage;

    if (bpp == 8) {
        uint32_t palette_size = src->infoHeader.biClrUsed;
        if (palette_size == 0) palette_size = 256;
        dst->palette = (unsigned char *)malloc(palette_size * 4);
        if (!dst->palette) { free_bmp(dst); return NULL; }
        memcpy(dst->palette, src->palette, palette_size * 4);
        dst->infoHeader.biClrUsed = palette_size;
    }

    dst->data = (unsigned char *)calloc(1, dst->infoHeader.biSizeImage);
    if (!dst->data) { free_bmp(dst); return NULL; }

    float inv_scale = 1.0f / scale;

    if (bpp == 8) {
        for (int y = 0; y < dst_h; ++y) {
            for (int x = 0; x < dst_w; ++x) {
                float src_x = x * inv_scale;
                float src_y = y * inv_scale;

                unsigned char value = 0;

                if (method == 'n') {
                    int nx = (int)(src_x + 0.5f);
                    int ny = (int)(src_y + 0.5f);
                    value = get_pixel_8(src, nx, ny);
                } else if (method == 'l') {
                    int x0 = (int)floorf(src_x);
                    int y0 = (int)floorf(src_y);
                    float dx = src_x - x0;
                    float dy = src_y - y0;
                    unsigned char p00 = get_pixel_8(src, x0, y0);
                    unsigned char p10 = get_pixel_8(src, x0 + 1, y0);
                    unsigned char p01 = get_pixel_8(src, x0, y0 + 1);
                    unsigned char p11 = get_pixel_8(src, x0 + 1, y0 + 1);
                    float v = (1 - dx) * (1 - dy) * p00 + dx * (1 - dy) * p10 + (1 - dx) * dy * p01 + dx * dy * p11;
                    value = (unsigned char)clampf(v, 0, 255);
                } else { /* cubic */
                    int x1 = (int)floorf(src_x);
                    int y1 = (int)floorf(src_y);
                    float sum = 0.0f;
                    float wsum = 0.0f;
                    for (int m = -1; m <= 2; ++m) {
                        for (int n = -1; n <= 2; ++n) {
                            float wx = cubic_weight((float)(x1 + n) - src_x);
                            float wy = cubic_weight((float)(y1 + m) - src_y);
                            float w = wx * wy;
                            unsigned char p = get_pixel_8(src, x1 + n, y1 + m);
                            sum += w * p;
                            wsum += w;
                        }
                    }
                    if (wsum != 0.0f) sum /= wsum;
                    value = (unsigned char)clampf(sum, 0, 255);
                }

                set_pixel_8(dst, x, y, value);
            }
        }
    } else {
        for (int y = 0; y < dst_h; ++y) {
            for (int x = 0; x < dst_w; ++x) {
                float src_x = x * inv_scale;
                float src_y = y * inv_scale;
                unsigned char out[3] = {0, 0, 0};

                if (method == 'n') {
                    int nx = (int)(src_x + 0.5f);
                    int ny = (int)(src_y + 0.5f);
                    get_pixel_24(src, nx, ny, out);
                } else if (method == 'l') {
                    int x0 = (int)floorf(src_x);
                    int y0 = (int)floorf(src_y);
                    float dx = src_x - x0;
                    float dy = src_y - y0;
                    unsigned char p00[3], p10[3], p01[3], p11[3];
                    get_pixel_24(src, x0, y0, p00);
                    get_pixel_24(src, x0 + 1, y0, p10);
                    get_pixel_24(src, x0, y0 + 1, p01);
                    get_pixel_24(src, x0 + 1, y0 + 1, p11);
                    for (int c = 0; c < 3; ++c) {
                        float v = (1 - dx) * (1 - dy) * p00[c] + dx * (1 - dy) * p10[c] + (1 - dx) * dy * p01[c] + dx * dy * p11[c];
                        out[c] = (unsigned char)clampf(v, 0, 255);
                    }
                } else { /* cubic */
                    int x1 = (int)floorf(src_x);
                    int y1 = (int)floorf(src_y);
                    float sum[3] = {0.0f, 0.0f, 0.0f};
                    float wsum = 0.0f;
                    for (int m = -1; m <= 2; ++m) {
                        for (int n = -1; n <= 2; ++n) {
                            float wx = cubic_weight((float)(x1 + n) - src_x);
                            float wy = cubic_weight((float)(y1 + m) - src_y);
                            float w = wx * wy;
                            unsigned char p[3];
                            get_pixel_24(src, x1 + n, y1 + m, p);
                            for (int c = 0; c < 3; ++c) sum[c] += w * p[c];
                            wsum += w;
                        }
                    }
                    if (wsum != 0.0f) {
                        for (int c = 0; c < 3; ++c) sum[c] /= wsum;
                    }
                    for (int c = 0; c < 3; ++c) out[c] = (unsigned char)clampf(sum[c], 0, 255);
                }

                set_pixel_24(dst, x, y, out);
            }
        }
    }

    return dst;
}

static void print_usage(void) {
    printf("Usage:\n");
    printf("  imgproc -z -m n|l|c input.bmp scale_percent output.bmp\n");
}

int main(int argc, char *argv[]) {
    if (argc != 6) {
        print_usage();
        return 1;
    }

    if (strcmp(argv[1], "-z") != 0 || strcmp(argv[2], "-m") != 0) {
        print_usage();
        return 1;
    }

    char method = argv[3][0];
    if (method != 'n' && method != 'l' && method != 'c') {
        print_usage();
        return 1;
    }

    const char *input = argv[4];
    int scale_percent = atoi(argv[5 - 1]);
    const char *output = argv[5];

    if (scale_percent < 1 || scale_percent > 999) {
        fprintf(stderr, "Scale percent must be 1-999.\n");
        return 1;
    }

    BMPIMAGE *src = read_bmp(input);
    if (!src) {
        fprintf(stderr, "Failed to read BMP: %s\n", input);
        return 1;
    }

    float scale = scale_percent / 100.0f;
    BMPIMAGE *dst = create_scaled(src, scale, method);
    if (!dst) {
        fprintf(stderr, "Failed to scale image.\n");
        free_bmp(src);
        return 1;
    }

    if (!write_bmp(output, dst)) {
        fprintf(stderr, "Failed to write BMP: %s\n", output);
        free_bmp(src);
        free_bmp(dst);
        return 1;
    }

    free_bmp(src);
    free_bmp(dst);

    return 0;
}
