/**
 * 封装libjpeg和libpng的调用
 *
 * @Author Tristan Sun
 * @Data 2012-11-09
 */

#ifndef __RRIMAGELIB_H
#define __RRIMAGELIB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <setjmp.h>
#include <math.h>
#include <png.h>
#include <jpeglib.h>

#include "libnsgif.h"

#define RR_DEBUG 1
#define LOG_TAG "rrimagelib"

#define PNG_MAGIC_SIZE 4

// 图片类型常量
#define TYPE_RRIMAGE_UNSPECIFIED 0
#define TYPE_RRIMAGE_JPEG 1
#define TYPE_RRIMAGE_PNG 2
#define TYPE_RRIMAGE_BMP 3
#define TYPE_RRIMAGE_GIF 4

// 压缩常量
#define COMPRESS_MAX_WIDTH 1600
#define COMPRESS_MIN_WIDTH 960

// 定义Exif旋转常量
#define ROTATE_0        1 //不旋转
#define ROTATE_90       6 //逆时针90
#define ROTATE_180		3 //逆时针180
#define ROTATE_270		8 //逆时针270
#define FLIP_ROTATE_0	2 //左右镜像
#define FLIP_ROTATE_90	5 //左右镜像后逆时针90
#define FLIP_ROTATE_180 4 //左右镜像后逆时针180
#define FLIP_ROTATE_270 7 //左右镜像后逆时针270
#if RR_DEBUG
#define LOGD(...) (printf(__VA_ARGS__))
#else
#define LOGD(...)
#endif

#undef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#undef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#undef CLAMP
#define CLAMP(a)  (MIN(255, MAX(0, (a))))

typedef void (*COMPRESS_METHOD)(int, int, int*, int*, int);

typedef struct {
    unsigned char r;  // [0, 255]
    unsigned char g;// [0, 255]
    unsigned char b;// [0, 255]
}RGB;

typedef struct {
    unsigned int width;	//宽
    unsigned int height;//高
    unsigned int channels;//通道数
    unsigned int stride;//一行字节数，一般为宽＊通道
    unsigned char *pixels;
    unsigned char type;// 图片源为jpeg格式或png格式，TYPE_RRIMAGE_JPEG表示jpeg，TYPE_RRIMAGE_PNG表示png，TYPE_RRIMAGE_UNSPECIFIED表示未知
    unsigned char quality;// 图片质量
}rrimage;

typedef struct my_error_mgr {
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
}* my_error_ptr;

rrimage *init_rrimage();

void free_rrimage(rrimage *);

rrimage * clone_rrimage(rrimage *);

/**
 * RGBA数据转换为RGB数据，忽略A通道
 */
void strip_alpha(rrimage *);

int check_file_type(FILE *);

void my_error_exit(j_common_ptr cinfo);

void my_output_message(j_common_ptr cinfo);

rrimage* read_jpeg(const char *);

int write_jpeg(const char *, rrimage *);

rrimage* read_png(const char *);

int write_png(const char *, rrimage *);

/**
 * 暂未处理RLE4和RLE8压缩的图像，有需求再加入
 */
rrimage* read_bmp(const char *);

/**
 * gif图一般不会很大，不考虑超大图的情况
 */
rrimage* read_gif(const char *file_path);

/**
 * 默认写入的bmp文件均为24位图像
 */
int write_bmp(const char *, rrimage *);

rrimage* read_image(const char *);

int write_image(const char *, rrimage *);

/**
 * 按照需求读取大图片并压缩到合适的大小
 *
 * @param file_path	文件路径
 * @param compress_method 压缩策略
 * @param min_width短边最小长度
 *
 * @return 返回图片数据
 */
rrimage* read_image_with_compress(const char *file_path,
        COMPRESS_METHOD compress_method, int min_width);

/**
 * 读取图片的某一部分区域，区域由参数x,y,w,h给出，区域内的图片按照compress_method指定的规则进行压缩
 *
 * <p>
 * rotate指定了图片的旋转方式，x, y, w, h指定了裁剪区域相对于图片旋转后的位置，x，y为左上角坐标，
 * w，h为宽度和高度，compress_method规则对裁剪出的图片应用
 * </p>
 *
 * @param file_path 图片输入路径
 * @param compress_method 压缩策略（不是对原图应用，而是对裁剪区域应用）
 * @param min_width短边最小长度
 * @param x	裁剪区域在按rotate旋转后的图片中的相对位置的左上角横坐标
 * @param y 裁剪区域在按rotate旋转后的图片中的相对位置的左上角纵坐标
 * @param w 裁剪区域宽度
 * @param h 裁剪区域高度
 */
rrimage* read_image_with_compress_by_area(const char *file_path,
        COMPRESS_METHOD compress_method, int min_width, int x, int y, int w, int h,
        int rotate);

/**
 * 图片压缩策略
 *
 * <p>
 * 1.短边超过minWidth，则短边压缩到minWidth；
 * 2.图片内存上限10M，如超过即便短边不超过minWidth，也按比例压缩
 * 3.长边按比例压缩
 * </p>
 *
 * @param width	图片原始宽度
 * @param height 图片原始高度
 * @param out_width 图片压缩后的宽度
 * @param out_height 图片压缩后的高度
 * @param minWidth 短边的最大长度
 */
void compress_strategy(int width, int height, int *out_width, int *out_height, int minWidth);

/**
 * 8种旋转方式
 */
void flip_or_rotate(rrimage *data, int orientation);

#ifdef __cplusplus
}
#endif

#endif
