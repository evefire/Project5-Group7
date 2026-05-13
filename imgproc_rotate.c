#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/*
 * 模块说明：BMP 旋转程序（imgproc -r）
 * 目的：读取 8/24 位无压缩 BMP，按指定角度顺时针旋转并保存。
 * 思路：读取 BMP -> 根据角度计算目标尺寸 -> 像素坐标映射。
 * 最小功能：支持 -r input.bmp 0|90|180|270 output.bmp。
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
    unsigned char *palette;
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

    if (img->fileHeader.bfType != 0x4D42) {
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

/*
 * 旋转核心：根据角度映射源坐标到目标坐标。
 * 最小功能：支持 0/90/180/270 四种角度。
 */
static BMPIMAGE *create_rotated(const BMPIMAGE *src, int angle) {
    int src_w = src->infoHeader.biWidth;
    int src_h = abs(src->infoHeader.biHeight);
    int bpp = src->infoHeader.biBitCount;

    int dst_w = src_w;
    int dst_h = src_h;
    if (angle == 90 || angle == 270) {
        dst_w = src_h;
        dst_h = src_w;
    }

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

    if (bpp == 8) {
        for (int y = 0; y < dst_h; ++y) {
            for (int x = 0; x < dst_w; ++x) {
                int sx = x;
                int sy = y;
                if (angle == 90) {
                    sx = y;
                    sy = src_h - 1 - x;
                } else if (angle == 180) {
                    sx = src_w - 1 - x;
                    sy = src_h - 1 - y;
                } else if (angle == 270) {
                    sx = src_w - 1 - y;
                    sy = x;
                }
                unsigned char value = get_pixel_8(src, sx, sy);
                set_pixel_8(dst, x, y, value);
            }
        }
    } else {
        for (int y = 0; y < dst_h; ++y) {
            for (int x = 0; x < dst_w; ++x) {
                int sx = x;
                int sy = y;
                if (angle == 90) {
                    sx = y;
                    sy = src_h - 1 - x;
                } else if (angle == 180) {
                    sx = src_w - 1 - x;
                    sy = src_h - 1 - y;
                } else if (angle == 270) {
                    sx = src_w - 1 - y;
                    sy = x;
                }
                unsigned char bgr[3];
                get_pixel_24(src, sx, sy, bgr);
                set_pixel_24(dst, x, y, bgr);
            }
        }
    }

    return dst;
}

static void print_usage(void) {
    printf("Usage:\n");
    printf("  imgproc -r input.bmp angle output.bmp\n");
    printf("  angle: 0 | 90 | 180 | 270\n");
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        print_usage();
        return 1;
    }

    if (strcmp(argv[1], "-r") != 0) {
        print_usage();
        return 1;
    }

    const char *input = argv[2];
    int angle = atoi(argv[3]);
    const char *output = argv[4];

    if (!(angle == 0 || angle == 90 || angle == 180 || angle == 270)) {
        print_usage();
        return 1;
    }

    BMPIMAGE *src = read_bmp(input);
    if (!src) {
        fprintf(stderr, "Failed to read BMP: %s\n", input);
        return 1;
    }

    BMPIMAGE *dst = create_rotated(src, angle);
    if (!dst) {
        fprintf(stderr, "Failed to rotate image.\n");
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
