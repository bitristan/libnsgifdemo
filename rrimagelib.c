#include "rrimagelib.h"

rrimage *init_rrimage() {
    rrimage *data = (rrimage *) malloc(sizeof(rrimage));
    data->pixels = NULL;
    data->type = TYPE_RRIMAGE_UNSPECIFIED;
    data->quality = 100;

    return data;
}

void free_rrimage(rrimage *data) {
    if (!data) {
        return;
    }

    if (data->pixels) {
        free(data->pixels);
        data->pixels = NULL;
    }

    free(data);
    data = NULL;
}

rrimage* clone_rrimage(rrimage *data) {
    if (!data) {
        return NULL;
    }

    rrimage *result = (rrimage *) malloc(sizeof(rrimage));

    int width = data->width;
    int height = data->height;
    int channels = data->channels;
    int stride = data->stride;

    result->width = width;
    result->height = height;
    result->channels = channels;
    result->stride = stride;
    result->type = data->type;
    result->quality = data->quality;

    if (data->pixels) {
        int size = stride * height;
        result->pixels = (unsigned char *) malloc(size);
        memcpy(result->pixels, data->pixels, size);
    }

    return result;
}

void strip_alpha(rrimage *data) {
    if (data == NULL || data->channels != 4) {
        return;
    }

    int width = data->width;
    int height = data->height;
    int channels = 3;
    int stride = width * channels;
    unsigned char *pixels_temp = data->pixels;

    data->channels = channels;
    data->stride = stride;
    data->pixels = (unsigned char *) malloc(stride * height);

    unsigned char *sptr = pixels_temp;
    unsigned char *dptr = data->pixels;
    int i, j;
    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
            dptr[0] = 255 - (255 - sptr[0]) * sptr[3] / 255;
            dptr[1] = 255 - (255 - sptr[1]) * sptr[3] / 255;
            dptr[2] = 255 - (255 - sptr[2]) * sptr[3] / 255;

            sptr += 4;
            dptr += 3;
        }
    }

    // free original memory
    free(pixels_temp);
}

int check_file_type(FILE *fp) {
    if (!fp) {
        return TYPE_RRIMAGE_UNSPECIFIED;
    }

    fseek(fp, 0L, SEEK_SET);
    char buf[PNG_MAGIC_SIZE];
    if (fread(buf, sizeof(char), PNG_MAGIC_SIZE, fp) != PNG_MAGIC_SIZE) {
        return TYPE_RRIMAGE_UNSPECIFIED;
    }
    if ((buf[0] == (char) 0xFF) && (buf[1] = (char) 0xD8)) {
        fseek(fp, 0L, SEEK_SET);
        return TYPE_RRIMAGE_JPEG;
    }
    if (!png_sig_cmp((png_byte *) buf, (png_size_t) 0, PNG_MAGIC_SIZE)) {
        fseek(fp, 0L, SEEK_SET);
        return TYPE_RRIMAGE_PNG;
    }
    if ((buf[0] == 'B') && (buf[1] == 'M')) {
        fseek(fp, 0L, SEEK_SET);
        return TYPE_RRIMAGE_BMP;
    }
    if ((buf[0] == 'G') && (buf[1] == 'I') && (buf[2] == 'F')) {
        fseek(fp, 0L, SEEK_SET);
        return TYPE_RRIMAGE_GIF;
    }

    fseek(fp, 0L, SEEK_SET);
    return TYPE_RRIMAGE_UNSPECIFIED;
}

void my_error_exit(j_common_ptr cinfo) {
    my_error_ptr myerr = (my_error_ptr) cinfo->err;
    (*cinfo->err->output_message)(cinfo);
    longjmp(myerr->setjmp_buffer, 1);
}

void my_output_message(j_common_ptr cinfo) {
    char buffer[JMSG_LENGTH_MAX];

    (*cinfo->err->format_message)(cinfo, buffer);
    LOGD("Output_message: %s\n", buffer);
}

rrimage* read_jpeg(const char *file_name) {
    if (!file_name) {
        return NULL;
    }

    FILE *in_file;
    if ((in_file = fopen(file_name, "rb")) == NULL) {
        return NULL;
    }

    rrimage *data = init_rrimage();

    struct jpeg_decompress_struct in;
    struct my_error_mgr in_err;

    in.err = jpeg_std_error(&in_err.pub);
    in_err.pub.error_exit = my_error_exit;
    in_err.pub.output_message = my_output_message;
    if (setjmp(in_err.setjmp_buffer)) {
        jpeg_destroy_decompress(&in);
        fclose(in_file);
        return NULL;
    }

    jpeg_create_decompress(&in);
    jpeg_stdio_src(&in, in_file);
    jpeg_read_header(&in, TRUE);

    jpeg_start_decompress(&in);

    data->width = in.image_width;
    data->height = in.image_height;
    data->channels = 3;
    data->stride = in.image_width * 3 * sizeof(unsigned char);
    data->type = TYPE_RRIMAGE_JPEG;

    // 灰度图将会转换为RGB
    data->pixels = (unsigned char *) malloc(data->stride * data->height);
    if (!data->pixels) {
        LOGD("out of memory when read jpeg file...");
        jpeg_destroy_decompress(&in);
        fclose(in_file);
        return NULL;
    }

    JSAMPROW row_pointer[1];

    int channels = in.output_components;
    if (channels == 3) {
        while (in.output_scanline < data->height) {
            row_pointer[0] = (&data->pixels[in.output_scanline * data->stride]);
            jpeg_read_scanlines(&in, row_pointer, 1);
        }
    } else if (channels == 1) {
        int i;
        unsigned char *line;
        row_pointer[0] = (unsigned char *) malloc(data->width);
        while (in.output_scanline < data->height) {
            line = &data->pixels[in.output_scanline * data->stride];
            jpeg_read_scanlines(&in, row_pointer, 1);
            for (i = 0; i < data->width; i++, line += 3) {
                line[0] = row_pointer[0][i];
                line[1] = line[0];
                line[2] = line[0];
            }
        }
        free(row_pointer[0]);
    } else {
        LOGD("unsupported jpeg format...channels = %d", channels);
        jpeg_destroy_decompress(&in);
        fclose(in_file);
        free_rrimage(data);
        return NULL;
    }

    jpeg_finish_decompress(&in);
    jpeg_destroy_decompress(&in);
    fclose(in_file);

    return data;
}

int write_jpeg(const char *file_name, rrimage *data) {
    if (file_name == NULL || data == NULL || data->pixels == NULL) {
        return 0;
    }

    FILE *out_file;
    if ((out_file = fopen(file_name, "wb")) == NULL) {
        return 0;
    }

    struct jpeg_compress_struct out;
    struct my_error_mgr out_err;

    out.err = jpeg_std_error(&out_err.pub);
    out_err.pub.error_exit = my_error_exit;
    if (setjmp(out_err.setjmp_buffer)) {
        jpeg_destroy_compress(&out);
        fclose(out_file);
        remove(file_name);
        return 0;
    }

    jpeg_create_compress(&out);
    jpeg_stdio_dest(&out, out_file);

    out.image_width = data->width;
    out.image_height = data->height;

    if (data->channels == 4) {
        strip_alpha(data);
    }
    out.input_components = data->channels;

    if (out.input_components == 1) {
        out.in_color_space = JCS_GRAYSCALE;
    } else {
        out.in_color_space = JCS_RGB;
    }

    jpeg_set_defaults(&out);
    jpeg_set_quality(&out, data->quality, TRUE);
    jpeg_start_compress(&out, TRUE);

    JSAMPROW row_pointer[1];
    int row_stride = data->width * data->channels;

    while (out.next_scanline < out.image_height) {
        row_pointer[0] = &data->pixels[out.next_scanline * row_stride];
        jpeg_write_scanlines(&out, row_pointer, 1);
    }

    jpeg_finish_compress(&out);
    jpeg_destroy_compress(&out);
    fclose(out_file);

    return 1;
}

rrimage* read_png(const char *file_name) {
    if (file_name == NULL) {
        return NULL;
    }

    FILE *in_file;
    if ((in_file = fopen(file_name, "rb")) == NULL) {
        return NULL;
    }

    rrimage *data = init_rrimage();

    png_structp in_png_ptr;
    png_infop in_info_ptr;

    in_png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL,
            NULL);
    if (in_png_ptr == NULL) {
        fclose(in_file);
        return NULL;
    }

    in_info_ptr = png_create_info_struct(in_png_ptr);
    if (in_info_ptr == NULL) {
        png_destroy_read_struct(&in_png_ptr, NULL, NULL);
        fclose(in_file);
        return NULL;
    }

    if (setjmp(png_jmpbuf(in_png_ptr))) {
        png_destroy_read_struct(&in_png_ptr, &in_info_ptr, NULL);
        fclose(in_file);
        return NULL;
    }

    png_init_io(in_png_ptr, in_file);
    png_read_info(in_png_ptr, in_info_ptr);

    data->width = png_get_image_width(in_png_ptr, in_info_ptr);
    data->height = png_get_image_height(in_png_ptr, in_info_ptr);
    int color_type = png_get_color_type(in_png_ptr, in_info_ptr);
    int bit_depth = png_get_bit_depth(in_png_ptr, in_info_ptr);

    // strip alpha channel
    // png_set_strip_alpha(in_png_ptr);
    // expand images of all color-type and bit-depth to 3x8 bit RGB images
    if (bit_depth == 16) {
        png_set_strip_16(in_png_ptr);
    }
    if (bit_depth < 8) {
        png_set_expand(in_png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_PALETTE) {
        png_set_palette_to_rgb(in_png_ptr);
    }
    if (color_type == PNG_COLOR_TYPE_GRAY
            || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(in_png_ptr);
    }

    png_read_update_info(in_png_ptr, in_info_ptr);

    color_type = png_get_color_type(in_png_ptr, in_info_ptr);
    int row_stride = 0;
    png_bytep row_pointers[1];

    if (color_type == PNG_COLOR_TYPE_RGB) {
        data->channels = 3;
    } else if (color_type == PNG_COLOR_TYPE_RGBA) {
        data->channels = 4;
    } else {
        LOGD("png file is not the valid format...color_type is %d", color_type);
        png_destroy_read_struct(&in_png_ptr, &in_info_ptr, NULL);
        fclose(in_file);
        return NULL;
    }

    row_stride = data->width * data->channels;
    data->stride = row_stride;
    data->type = TYPE_RRIMAGE_PNG;
    data->pixels = (unsigned char *) malloc(
            sizeof(unsigned char) * row_stride * data->height);
    if (!data->pixels) {
        LOGD("out of memory when read png file...");
        png_destroy_read_struct(&in_png_ptr, &in_info_ptr, NULL);
        fclose(in_file);
        return NULL;
    }

    int i;
    for (i = 0; i < data->height; i++) {
        row_pointers[0] = &data->pixels[i * row_stride];
        png_read_rows(in_png_ptr, row_pointers, NULL, 1);
    }

    png_destroy_read_struct(&in_png_ptr, &in_info_ptr, NULL);
    fclose(in_file);

    return data;
}

int write_png(const char *file_name, rrimage *data) {
    if (file_name == NULL || data == NULL || data->pixels == NULL) {
        return 0;
    }

    FILE *out_file;
    if ((out_file = fopen(file_name, "wb")) == NULL) {
        return 0;
    }

    png_structp out_png_ptr;
    png_infop out_info_ptr;

    out_png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL,
            NULL);
    if (out_png_ptr == NULL) {
        fclose(out_file);
        remove(file_name);
        return 0;
    }

    out_info_ptr = png_create_info_struct(out_png_ptr);
    if (out_info_ptr == NULL) {
        fclose(out_file);
        remove(file_name);
        return 0;
    }

    if (setjmp(png_jmpbuf(out_png_ptr))) {
        png_destroy_write_struct(&out_png_ptr, &out_info_ptr);
        fclose(out_file);
        remove(file_name);
        return 0;
    }

    int color_type = PNG_COLOR_TYPE_RGB;
    if (data->channels == 4) {
        // only read in alpha mode, not write alpha channel.
        strip_alpha(data);
        color_type = PNG_COLOR_TYPE_RGB;
    } else if (data->channels == 1) {
        color_type = PNG_COLOR_TYPE_GRAY;
    }

    png_init_io(out_png_ptr, out_file);
    png_set_IHDR(out_png_ptr, out_info_ptr, data->width, data->height, 8,
            color_type, PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE,
            PNG_FILTER_TYPE_BASE);
    png_set_strip_alpha(out_png_ptr);

    // write header
    if (setjmp(png_jmpbuf(out_png_ptr))) {
        png_destroy_write_struct(&out_png_ptr, &out_info_ptr);
        fclose(out_file);
        remove(file_name);
        return 0;
    }
    png_write_info(out_png_ptr, out_info_ptr);

    // write data line by line
    if (setjmp(png_jmpbuf(out_png_ptr))) {
        png_destroy_write_struct(&out_png_ptr, &out_info_ptr);
        fclose(out_file);
        remove(file_name);
        return 0;
    }
    png_bytep row_pointer[1];
    int i;
    for (i = 0; i < data->height; i++) {
        row_pointer[0] = &data->pixels[i * data->width * data->channels];
        png_write_rows(out_png_ptr, row_pointer, 1);
    }

    // write end
    if (setjmp(png_jmpbuf(out_png_ptr))) {
        png_destroy_write_struct(&out_png_ptr, &out_info_ptr);
        fclose(out_file);
        remove(file_name);
        return 0;
    }
    png_write_end(out_png_ptr, NULL);

    png_destroy_write_struct(&out_png_ptr, &out_info_ptr);
    fclose(out_file);

    return 1;
}

rrimage* read_bmp(const char *file_name) {
    if (file_name == NULL) {
        return NULL;
    }

    FILE *in_file;
    if ((in_file = fopen(file_name, "rb")) == NULL) {
        return NULL;
    }

    char magic[2];
    if (fread(magic, sizeof(magic), 1, in_file) != 1) {
        LOGD("read bmp header error");
        fclose(in_file);
        return 0;
    }

    if (magic[0] != 'B' || magic[1] != 'M') {
        LOGD("file is not in bmp format...");
        fclose(in_file);
        return 0;
    }

    unsigned int size;
    unsigned int bitmap_offset;
    unsigned int header_size;
    int width;
    int height;
    unsigned short bits_per_pixel;
    unsigned int compression;
    unsigned int bitmap_size;
    unsigned int num_colors;

    // read bmp header
    if (fread(&size, sizeof(size), 1, in_file) != 1) {
        LOGD("read bmp file header error\n");
        fclose(in_file);
        return 0;
    }
    fseek(in_file, 4, SEEK_CUR);
    if (fread(&bitmap_offset, sizeof(bitmap_offset), 1, in_file) != 1) {
        LOGD("read bmp file header error\n");
        fclose(in_file);
        return 0;
    }
    if (fread(&header_size, sizeof(header_size), 1, in_file) != 1) {
        LOGD("read bmp file header error\n");
        fclose(in_file);
        return 0;
    }
    if (fread(&width, sizeof(width), 1, in_file) != 1) {
        LOGD("read bmp file header error\n");
        fclose(in_file);
        return 0;
    }
    if (fread(&height, sizeof(height), 1, in_file) != 1) {
        LOGD("read bmp file header error\n");
        fclose(in_file);
        return 0;
    }
    fseek(in_file, 2L, SEEK_CUR);
    if (fread(&bits_per_pixel, sizeof(bits_per_pixel), 1, in_file) != 1) {
        LOGD("read bmp file header error\n");
        fclose(in_file);
        return 0;
    }
    if (fread(&compression, sizeof(compression), 1, in_file) != 1) {
        LOGD("read bmp file header error\n");
        fclose(in_file);
        return 0;
    }
    if (fread(&bitmap_size, sizeof(bitmap_size), 1, in_file) != 1) {
        LOGD("read bmp file header error\n");
        fclose(in_file);
        return 0;
    }
    fseek(in_file, 8L, SEEK_CUR);
    if (fread(&num_colors, sizeof(num_colors), 1, in_file) != 1) {
        LOGD("read bmp file header error\n");
        fclose(in_file);
        return 0;
    }

    // 暂时只支持不压缩的格式，不支持RLE4和RLE8压缩格式（目前bmp图片基本都不压缩，需要再看情况适配）
    if (compression != 0) {
        LOGD("can't support RLE4 or RLE8 compression at present...");
        fclose(in_file);
        return 0;
    }

    // begin to read image data.
    fseek(in_file, bitmap_offset, SEEK_SET);
    // 计算每一行字节数(不知为啥，bitmap_size不可信，有时会为0)
    // int stride = bitmap_size / height;
    int channels;
    int stride;
    if (bits_per_pixel == 24) {
        channels = 3;
        stride = (width * 24 + 31) / 32 * 4;
    } else if (bits_per_pixel == 32) {
        channels = 4;
        stride = width * 4;
    } else {
        LOGD("only support 24bits and 32bits per pixel yet...");
        fclose(in_file);
        return 0;
    }

    unsigned char *pixels = (unsigned char *) malloc(
            width * height * channels * sizeof(unsigned char));
    if (!pixels) {
        LOGD("out of memory when load bmp data..");
        fclose(in_file);
        return 0;
    }

    int i, j;
    unsigned char *line;

    // 暂时只支持24位图和32位图。。。呃。。。看情况再增加吧。。
    switch (bits_per_pixel) {
    case 24:
        // Order as BGR.
        for (i = 0; i < height; i++) {
            line = &pixels[(height - 1 - i) * width * channels];
            for (j = 0; j < width; j++, line += channels) {
                if (fread(line + 2, 1, 1, in_file) != 1) {
                    LOGD("read bmp image data error\n");
                    free(pixels);
                    fclose(in_file);
                    return 0;
                }
                if (fread(line + 1, 1, 1, in_file) != 1) {
                    LOGD("read bmp image data error\n");
                    free(pixels);
                    fclose(in_file);
                    return 0;
                }
                if (fread(line, 1, 1, in_file) != 1) {
                    LOGD("read bmp image data error\n");
                    free(pixels);
                    fclose(in_file);
                    return 0;
                }
            }
            fseek(in_file, (stride - width * channels), SEEK_CUR);
        }

        break;
    case 32:
        // Order as BGRA.
        for (i = 0; i < height; i++) {
            line = &pixels[(height - 1 - i) * width * channels];
            for (j = 0; j < width; j++, line += channels) {
                if (fread(line + 2, 1, 1, in_file) != 1) {
                    LOGD("read bmp image data error\n");
                    free(pixels);
                    fclose(in_file);
                    return 0;
                }
                if (fread(line + 1, 1, 1, in_file) != 1) {
                    LOGD("read bmp image data error\n");
                    free(pixels);
                    fclose(in_file);
                    return 0;
                }
                if (fread(line, 1, 1, in_file) != 1) {
                    LOGD("read bmp image data error\n");
                    free(pixels);
                    fclose(in_file);
                    return 0;
                }
                if (fread(line + 3, 1, 1, in_file) != 1) {
                    LOGD("read bmp image data error\n");
                    free(pixels);
                    fclose(in_file);
                    return 0;
                }
            }
            fseek(in_file, (stride - width * channels), SEEK_CUR);
        }
        break;
    default:
        break;
    }

    fclose(in_file);

    rrimage *data = init_rrimage();
    data->width = width;
    data->height = height;
    data->channels = channels;
    data->stride = width * channels;
    data->type = TYPE_RRIMAGE_BMP;
    data->pixels = pixels;

    if (channels == 4) {
        strip_alpha(data);
    }

    return data;
}

rrimage* read_gif(const char *file_path) {
    if (!file_path) {
        return NULL;
    }

    gif_bitmap_callback_vt bitmap_callbacks = { bitmap_create, bitmap_destroy,
            bitmap_get_buffer, bitmap_set_opaque, bitmap_test_opaque,
            bitmap_modified };
    gif_animation gif;
    size_t size;
    gif_result code;
    unsigned int i;

    gif_create(&gif, &bitmap_callbacks);

    unsigned char *data = load_file(file_path, &size);
    do {
        code = gif_initialise(&gif, size, data);
        if (code != GIF_OK && code != GIF_WORKING) {
            return NULL;
        }
    } while (code != GIF_OK);

    int frame_count = gif.frame_count;
    if (frame_count < 1) {
        return NULL;
    }

    code = gif_decode_frame(&gif, 0);
    if (code != GIF_OK) {
        return NULL;
    }

    rrimage *result = init_rrimage();
    result->width = gif.width;
    result->height = gif.height;
    result->channels = 4;
    result->stride = gif.width * 4;
    result->pixels = (unsigned char *) malloc(gif.width * gif.height * 4);
    memcpy(result->pixels, gif.frame_image, gif.width * gif.height * 4);

    gif_finalise(&gif);
    free(data);

    return result;
}

/**
 * 这个方法有bug，待处理。。。。
 */
int write_bmp(const char *file_name, rrimage *data) {
    if (file_name == NULL || data == NULL || data->pixels == NULL) {
        return 0;
    }

    FILE *out_file;
    if ((out_file = fopen(file_name, "wb")) == NULL) {
        return 0;
    }

    int width = data->width;
    int height = data->height;
    // 默认写入24位图，暂不实现其他情况
    unsigned short bits_per_pixel = 24;
    // 计算每行字节数
    unsigned int stride = (width * bits_per_pixel + 31) / 32 * 4;
    unsigned int bitmap_size = stride * height;
    // 正常情况下文件头为54个字节，其中位图信息头占40个字节
    unsigned int bitmap_offset = 0x36;
    unsigned int header_size = 0x28;
    unsigned int file_size = bitmap_size + bitmap_offset;
    unsigned int reserved = 0;
    unsigned short planes = 1;
    unsigned int compression = 0;
    unsigned int horizontal_resolution = 72;
    unsigned int vertical_resolution = 72;
    unsigned int num_colors = 0;
    unsigned int num_important_colors = 0;

    // 写头信息
    char magic[2];
    magic[0] = 'B';
    magic[1] = 'M';
    if (fwrite(magic, sizeof(magic), 1, out_file) != 1) {
        LOGD("write bmp header error...");
        fclose(out_file);
        remove(file_name);
        return 0;
    }
    if (fwrite(&file_size, sizeof(file_size), 1, out_file) != 1) {
        LOGD("write bmp header error...");
        fclose(out_file);
        remove(file_name);
        return 0;
    }
    if (fwrite(&reserved, sizeof(reserved), 1, out_file) != 1) {
        LOGD("write bmp header error...");
        fclose(out_file);
        remove(file_name);
        return 0;
    }
    if (fwrite(&bitmap_offset, sizeof(bitmap_offset), 1, out_file) != 1) {
        LOGD("write bmp header error...");
        fclose(out_file);
        remove(file_name);
        return 0;
    }
    if (fwrite(&header_size, sizeof(header_size), 1, out_file) != 1) {
        LOGD("write bmp header error...");
        fclose(out_file);
        remove(file_name);
        return 0;
    }
    if (fwrite(&width, sizeof(width), 1, out_file) != 1) {
        LOGD("write bmp header error...");
        fclose(out_file);
        remove(file_name);
        return 0;
    }
    if (fwrite(&height, sizeof(height), 1, out_file) != 1) {
        LOGD("write bmp header error...");
        fclose(out_file);
        remove(file_name);
        return 0;
    }
    if (fwrite(&planes, sizeof(planes), 1, out_file) != 1) {
        LOGD("write bmp header error...");
        fclose(out_file);
        remove(file_name);
        return 0;
    }
    if (fwrite(&bits_per_pixel, sizeof(bits_per_pixel), 1, out_file) != 1) {
        LOGD("write bmp header error...");
        fclose(out_file);
        remove(file_name);
        return 0;
    }
    if (fwrite(&compression, sizeof(compression), 1, out_file) != 1) {
        LOGD("write bmp header error...");
        fclose(out_file);
        remove(file_name);
        return 0;
    }
    if (fwrite(&bitmap_size, sizeof(bitmap_size), 1, out_file) != 1) {
        LOGD("write bmp header error...");
        fclose(out_file);
        remove(file_name);
        return 0;
    }
    if (fwrite(&horizontal_resolution, sizeof(horizontal_resolution), 1,
            out_file) != 1) {
        LOGD("write bmp header error...");
        fclose(out_file);
        remove(file_name);
        return 0;
    }
    if (fwrite(&vertical_resolution, sizeof(vertical_resolution), 1, out_file)
            != 1) {
        LOGD("write bmp header error...");
        fclose(out_file);
        remove(file_name);
        return 0;
    }
    if (fwrite(&num_colors, sizeof(num_colors), 1, out_file) != 1) {
        LOGD("write bmp header error...");
        fclose(out_file);
        remove(file_name);
        return 0;
    }
    if (fwrite(&num_important_colors, sizeof(num_important_colors), 1, out_file)
            != 1) {
        LOGD("write bmp header error...");
        fclose(out_file);
        remove(file_name);
        return 0;
    }

    // 写图片数据，从下往上，从左往右
    int i, j;
    unsigned char *line;
    int src_stride = data->stride;
    int channels = data->channels;
    for (i = height; i > 0; i--) {
        line = &data->pixels[(i - 1) * src_stride];
        for (j = 0; j < width; j++, line += channels) {
            switch (channels) {
            case 1:
                // bmp顺序为BGR
                if (fputc(line[j], out_file) == EOF) {
                    LOGD("write bmp data error...");
                    fclose(out_file);
                    remove(file_name);
                    return 0;
                }
                if (fputc(line[j], out_file) == EOF) {
                    LOGD("write bmp data error...");
                    fclose(out_file);
                    remove(file_name);
                    return 0;
                }
                if (fputc(line[j], out_file) == EOF) {
                    LOGD("write bmp data error...");
                    fclose(out_file);
                    remove(file_name);
                    return 0;
                }
                break;
            case 3:
            case 4:
                if (fputc(line[j + 2], out_file) == EOF) {
                    LOGD("write bmp data error...");
                    fclose(out_file);
                    remove(file_name);
                    return 0;
                }
                if (fputc(line[j + 1], out_file) == EOF) {
                    LOGD("write bmp data error...");
                    fclose(out_file);
                    remove(file_name);
                    return 0;
                }
                if (fputc(line[j], out_file) == EOF) {
                    LOGD("write bmp data error...");
                    fclose(out_file);
                    remove(file_name);
                    return 0;
                }
                break;
            default:
                LOGD("image data to be written was the error format...");
                fclose(out_file);
                remove(file_name);
                return 0;
            }
        }

        // 补齐字节
        j *= 3;
        while (j++ < stride) {
            if (fputc(0, out_file) == EOF) {
                LOGD("write bmp data error...");
                fclose(out_file);
                remove(file_name);
                return 0;
            }
        }
    }

    LOGD("success write a bmp file...");

    fclose(out_file);
    return 1;
}

rrimage *read_image(const char *file_name) {
    return read_image_with_compress(file_name, NULL, 0);
}

rrimage* read_image_with_compress(const char *file_name,
        COMPRESS_METHOD compress_method, int min_width) {
    return read_image_with_compress_by_area(file_name, NULL, min_width, 0, 0, 0,
            0, ROTATE_0);
}

// 根据旋转角度映射剪切位置
void calculate_crop_area(int width, int height, int *x, int *y, int *w, int *h,
        int rotate) {
    int i;

    switch (rotate) {
    case ROTATE_0:
        break;
    case ROTATE_90:
        i = *x;
        *x = *y;
        *y = height - 1 - i - *w;

        i = *h;
        *h = *w;
        *w = i;
        break;
    case ROTATE_180:
        *x = width - 1 - *x - *w;
        *y = height - 1 - *y - *h;
        break;
    case ROTATE_270:
        i = *x;
        *x = width - 1 - *y - *h;
        *y = i;

        i = *h;
        *h = *w;
        *w = i;
        break;
    case FLIP_ROTATE_0:
        *x = width - 1 - *x - *w;
        break;
    case FLIP_ROTATE_90:
        i = *x;
        *x = *y;
        *y = i;

        i = *h;
        *h = *w;
        *w = i;
        break;
    case FLIP_ROTATE_180:
        *y = height - 1 - *y - *h;
        break;
    case FLIP_ROTATE_270:
        *x = width - 1 - *y - *w;
        *y = height - 1 - *x - *h;

        i = *h;
        *h = *w;
        *w = i;
        break;
    default:
        break;
    }

    // 处理裁剪参数异常的情况
    *x = *x < 0 ? 0 : *x > width - 1 ? width - 1 : *x;
    *y = *y < 0 ? 0 : *y > height - 1 ? height - 1 : *y;
    *w = *w > width ? width : *w;
    *h = *h > height ? height : *h;
    if (*w < 1 || *h < 1 || *x == width - 1 || *y == height - 1) {
        // 宽和高有一个小于1，或者裁剪左上角坐标在图片右下边缘，则不裁剪
        *x = 0;
        *y = 0;
        *w = width;
        *h = height;
    } else {
        if (*x + *w > width) {
            *w = width - *x;
        }
        if (*y + *h > height) {
            *h = height - *y;
        }
    }
}

rrimage* read_image_with_compress_by_area(const char *file_name,
        COMPRESS_METHOD compress_method, int min_width, int x, int y, int w,
        int h, int rotate) {
    int i, j;
    rrimage *data = NULL;

    FILE * in_file;
    if ((in_file = fopen(file_name, "rb")) == NULL) {
        return NULL;
    }
    int file_type = check_file_type(in_file);
    if (file_type == TYPE_RRIMAGE_JPEG) {
        struct jpeg_decompress_struct in;
        struct my_error_mgr in_err;

        in.err = jpeg_std_error(&in_err.pub);
        in_err.pub.error_exit = my_error_exit;
        in_err.pub.output_message = my_output_message;
        if (setjmp(in_err.setjmp_buffer)) {
            jpeg_destroy_decompress(&in);
            fclose(in_file);
            return 0;
        }

        jpeg_create_decompress(&in);
        jpeg_stdio_src(&in, in_file);
        jpeg_read_header(&in, TRUE);
        jpeg_start_decompress(&in);

        int width = in.image_width;
        int height = in.image_height;
        // GRAY将被转换为RGB
        int channels = 3;
        int components = in.output_components;

        calculate_crop_area(width, height, &x, &y, &w, &h, rotate);

        int out_width = w;
        int out_height = h;
        int out_stride;
        // 计算裁剪后的图片压缩后的宽高
        if (compress_method) {
            compress_method(w, h, &out_width, &out_height, min_width);
        }
        out_stride = out_width * channels * sizeof(unsigned char);
        int stride = width * components * sizeof(unsigned char);

        // 指向最终输出的图片全部数据
        unsigned char *pixels = (unsigned char *) malloc(
                out_height * out_stride);
        if (!pixels) {
            LOGD("out of memory when read png file...");
            jpeg_destroy_decompress(&in);
            fclose(in_file);
            return NULL;
        }

        JSAMPROW row_pointer[1];
        if (out_width == w) {
            // 指向原图中一行数据
            unsigned char *in_line_pointer = (unsigned char *) malloc(stride);

            if (components == 3) {
                for (i = 0; i < height; i++) {
                    row_pointer[0] = in_line_pointer;
                    jpeg_read_scanlines(&in, row_pointer, 1);

                    if (i < y) {
                        continue;
                    }

                    if (i > y + h - 1) {
                        break;
                    }

                    memcpy(&pixels[(i - y) * out_stride],
                            in_line_pointer + x * channels, out_stride);
                }
                free(in_line_pointer);
            } else if (components == 1) {
                unsigned char *line;
                for (i = 0; i < height; i++) {
                    row_pointer[0] = in_line_pointer;
                    jpeg_read_scanlines(&in, row_pointer, 1);

                    if (i < y) {
                        continue;
                    }

                    if (i > y + h - 1) {
                        break;
                    }

                    line = &pixels[(i - y) * out_stride];
                    for (j = 0; j < out_width; j++, line += channels) {
                        line[0] = in_line_pointer[x + j];
                        line[1] = line[0];
                        line[2] = line[0];
                    }
                }
                free(in_line_pointer);
            } else {
                LOGD("unsupported jpeg format...channels = %d",
                        in.output_components);
                jpeg_destroy_decompress(&in);
                fclose(in_file);
                free(pixels);
                return NULL;
            }
        } else {
            float scale = out_width / (float) w;

            unsigned char *base_line_pointer = (unsigned char *) malloc(stride);
            unsigned char *next_line_pointer = (unsigned char *) malloc(stride);
            unsigned char *out_line_pointer;

            int up_left, up_right, down_left, down_right;
            float fX, fY;
            int iX, iY;
            int c;
            int current_line = 0;

            // 先跳过y行
            for (i = 0; i < y; i++) {
                row_pointer[0] = base_line_pointer;
                jpeg_read_scanlines(&in, row_pointer, 1);
            }

            for (i = 0; i < out_height; i++) {
                out_line_pointer = &pixels[i * out_stride];

                fY = (float) (i + 1) / scale - 1;
                iY = (int) fY;

                if (iY != h - 1) {
                    if (current_line <= iY) {
                        row_pointer[0] = base_line_pointer;
                        for (c = 0; c < iY + 1 - current_line; c++) {
                            jpeg_read_scanlines(&in, row_pointer, 1);
                            current_line++;
                        }
                        row_pointer[0] = next_line_pointer;
                        jpeg_read_scanlines(&in, row_pointer, 1);
                        current_line++;
                    } else if (current_line == (iY + 1)) {
                        memcpy(base_line_pointer, next_line_pointer, stride);
                        row_pointer[0] = next_line_pointer;
                        jpeg_read_scanlines(&in, row_pointer, 1);
                        current_line++;
                    }
                } else {
                    // 如果是最后一行，那么base_line和next_line都指向最后一行数据
                    // 因为是压缩，此时in.output_scanline一定会小于等于iY
                    row_pointer[0] = base_line_pointer;
                    for (c = 0; c < iY + 1 - current_line; c++) {
                        jpeg_read_scanlines(&in, row_pointer, 1);
                        current_line++;
                    }
                    memcpy(next_line_pointer, base_line_pointer, stride);
                }

                if (components == 3) {
                    // 跳过x列
                    for (j = 0; j < out_width;
                            j++, out_line_pointer += channels) {
                        fX = (float) (j + 1) / scale - 1;
                        iX = (int) fX;

                        for (c = 0; c < channels; c++) {
                            up_left =
                                    base_line_pointer[(iX + x) * channels + c];
                            up_right = base_line_pointer[(iX + x + 1) * channels
                                    + c];
                            down_left = next_line_pointer[(iX + x) * channels
                                    + c];
                            down_right = next_line_pointer[(iX + x + 1)
                                    * channels + c];

                            out_line_pointer[c] =
                                    CLAMP((int) (up_left * (iX + 1 - fX) * (iY + 1 - fY)
                                                    + up_right * (fX - iX) * (iY + 1 - fY)
                                                    + down_left * (iX + 1 - fX) * (fY - iY)
                                                    + down_right * (fX - iX) * (fY - iY)));
                        }
                    }
                } else if (components == 1) {
                    // 跳过x列
                    for (j = 0; j < out_width;
                            j++, out_line_pointer += channels) {
                        fX = (float) (j + 1) / scale - 1;
                        iX = (int) fX;

                        up_left = base_line_pointer[(iX + x)];
                        up_right = base_line_pointer[(iX + x + 1)];
                        down_left = next_line_pointer[(iX + x)];
                        down_right = next_line_pointer[(iX + x + 1)];

                        out_line_pointer[0] =
                                CLAMP((int) (up_left * (iX + 1 - fX) * (iY + 1 - fY)
                                                + up_right * (fX - iX) * (iY + 1 - fY)
                                                + down_left * (iX + 1 - fX) * (fY - iY)
                                                + down_right * (fX - iX) * (fY - iY)));
                        out_line_pointer[1] = out_line_pointer[0];
                        out_line_pointer[2] = out_line_pointer[0];
                    }
                } else {
                    LOGD("unsupported jpeg format...channels = %d",
                            in.output_components);
                    jpeg_destroy_decompress(&in);
                    fclose(in_file);
                    free(pixels);
                    free(base_line_pointer);
                    free(next_line_pointer);
                    return NULL;
                }
            }

            free(base_line_pointer);
            free(next_line_pointer);
        }

        in.output_scanline = height;
        jpeg_finish_decompress(&in);
        jpeg_destroy_decompress(&in);
        fclose(in_file);

        data = init_rrimage();
        data->width = out_width;
        data->height = out_height;
        data->channels = channels;
        data->stride = out_stride;
        data->type = TYPE_RRIMAGE_JPEG;
        data->pixels = pixels;

        // 旋转处理
        flip_or_rotate(data, rotate);
    } else if (file_type == TYPE_RRIMAGE_PNG) {
        png_structp in_png_ptr;
        png_infop in_info_ptr;

        in_png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL,
                NULL);
        if (in_png_ptr == NULL) {
            fclose(in_file);
            return 0;
        }

        in_info_ptr = png_create_info_struct(in_png_ptr);
        if (in_info_ptr == NULL) {
            png_destroy_read_struct(&in_png_ptr, NULL, NULL);
            fclose(in_file);
            return 0;
        }

        if (setjmp(png_jmpbuf(in_png_ptr))) {
            png_destroy_read_struct(&in_png_ptr, &in_info_ptr, NULL);
            fclose(in_file);
            return 0;
        }

        png_init_io(in_png_ptr, in_file);
        png_read_info(in_png_ptr, in_info_ptr);

        int width = png_get_image_width(in_png_ptr, in_info_ptr);
        int height = png_get_image_height(in_png_ptr, in_info_ptr);
        int color_type = png_get_color_type(in_png_ptr, in_info_ptr);
        int bit_depth = png_get_bit_depth(in_png_ptr, in_info_ptr);
        int channels;

        // png_set_strip_alpha(in_png_ptr);
        if (bit_depth == 16) {
            png_set_strip_16(in_png_ptr);
        }
        if (bit_depth < 8) {
            png_set_expand(in_png_ptr);
        }
        if (color_type == PNG_COLOR_TYPE_PALETTE) {
            png_set_palette_to_rgb(in_png_ptr);
        }
        if (color_type == PNG_COLOR_TYPE_GRAY
                || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
            png_set_gray_to_rgb(in_png_ptr);
        }
        png_read_update_info(in_png_ptr, in_info_ptr);
        color_type = png_get_color_type(in_png_ptr, in_info_ptr);

        if (color_type == PNG_COLOR_TYPE_RGB) {
            channels = 3;
        } else if (color_type == PNG_COLOR_TYPE_RGBA) {
            channels = 4;
        } else {
            LOGD("png file format error...");
            png_destroy_read_struct(&in_png_ptr, &in_info_ptr, NULL);
            fclose(in_file);
            return NULL;
        }

        // 根据旋转角度映射剪切位置
        calculate_crop_area(width, height, &x, &y, &w, &h, rotate);

        int out_width = w;
        int out_height = h;
        int out_stride;
        // 计算裁剪后的图片压缩后的宽高
        if (compress_method) {
            compress_method(w, h, &out_width, &out_height, min_width);
        }
        out_stride = out_width * channels * sizeof(unsigned char);
        int stride = width * channels * sizeof(unsigned char);

        // 指向最终输出的图片全部数据
        unsigned char *pixels = (unsigned char *) malloc(
                out_height * out_stride);
        if (!pixels) {
            LOGD("out of memory when read png file...");
            png_destroy_read_struct(&in_png_ptr, &in_info_ptr, NULL);
            fclose(in_file);
            return NULL;
        }

        png_bytep row_pointer[1];
        if (out_width == w) {
            // 指向原图中一行数据
            unsigned char *in_line_pointer = (unsigned char *) malloc(stride);
            for (i = 0; i < height; i++) {
                row_pointer[0] = in_line_pointer;
                png_read_rows(in_png_ptr, row_pointer, NULL, 1);

                if (i < y) {
                    continue;
                }

                if (i > y + h - 1) {
                    break;
                }

                memcpy(&pixels[(i - y) * out_stride],
                        in_line_pointer + x * channels, out_stride);
            }
            free(in_line_pointer);
        } else {
            float scale = out_width / (float) w;

            unsigned char *base_line_pointer = (unsigned char *) malloc(stride);
            unsigned char *next_line_pointer = (unsigned char *) malloc(stride);
            unsigned char *out_line_pointer;

            int up_left, up_right, down_left, down_right;
            float fX, fY;
            int iX, iY;
            int c;
            int current_line = 0;

            // 先跳过y行
            for (i = 0; i < y; i++) {
                row_pointer[0] = base_line_pointer;
                png_read_rows(in_png_ptr, row_pointer, NULL, 1);
            }

            for (i = 0; i < out_height; i++) {
                out_line_pointer = &pixels[i * out_stride];

                fY = (float) (i + 1) / scale - 1;
                iY = (int) fY;

                if (iY != h - 1) {
                    if (current_line <= iY) {
                        row_pointer[0] = base_line_pointer;
                        for (c = 0; c < iY + 1 - current_line; c++) {
                            png_read_rows(in_png_ptr, row_pointer, NULL, 1);
                            current_line++;
                        }
                        row_pointer[0] = next_line_pointer;
                        png_read_rows(in_png_ptr, row_pointer, NULL, 1);
                        current_line++;
                    } else if (current_line == (iY + 1)) {
                        memcpy(base_line_pointer, next_line_pointer, stride);
                        row_pointer[0] = next_line_pointer;
                        png_read_rows(in_png_ptr, row_pointer, NULL, 1);
                        current_line++;
                    }
                } else {
                    // 如果是最后一行，那么base_line和next_line都指向最后一行数据
                    // 因为是压缩，此时current_line一定会小于等于iY
                    row_pointer[0] = base_line_pointer;
                    for (c = 0; c < iY + 1 - current_line; c++) {
                        png_read_rows(in_png_ptr, row_pointer, NULL, 1);
                        current_line++;
                    }
                    memcpy(next_line_pointer, base_line_pointer, stride);
                }

                // 跳过x列
                for (j = 0; j < out_width; j++, out_line_pointer += channels) {
                    fX = (float) (j + 1) / scale - 1;
                    iX = (int) fX;

                    for (c = 0; c < channels; c++) {
                        up_left = base_line_pointer[(iX + x) * channels + c];
                        up_right =
                                base_line_pointer[(iX + x + 1) * channels + c];
                        down_left = next_line_pointer[(iX + x) * channels + c];
                        down_right = next_line_pointer[(iX + x + 1) * channels
                                + c];

                        out_line_pointer[c] =
                                CLAMP((int) (up_left * (iX + 1 - fX) * (iY + 1 - fY)
                                                + up_right * (fX - iX) * (iY + 1 - fY)
                                                + down_left * (iX + 1 - fX) * (fY - iY)
                                                + down_right * (fX - iX) * (fY - iY)));
                    }
                }
            }

            free(base_line_pointer);
            free(next_line_pointer);
        }

        png_destroy_read_struct(&in_png_ptr, &in_info_ptr, NULL);
        fclose(in_file);

        data = init_rrimage();
        data->width = out_width;
        data->height = out_height;
        data->channels = channels;
        data->stride = out_stride;
        data->type = TYPE_RRIMAGE_PNG;
        data->pixels = pixels;

        // 旋转处理
        flip_or_rotate(data, rotate);
    } else if (file_type == TYPE_RRIMAGE_BMP) {
        unsigned int size;
        unsigned int bitmap_offset;
        unsigned int header_size;
        int width;
        int height;
        unsigned short bits_per_pixel;
        unsigned int compression;
        unsigned int bitmap_size;
        unsigned int num_colors;

        fseek(in_file, 2L, SEEK_CUR);
        // read bmp header
        if (fread(&size, sizeof(size), 1, in_file) != 1) {
            LOGD("read bmp file header error\n");
            fclose(in_file);
            return 0;
        }
        fseek(in_file, 4, SEEK_CUR);
        if (fread(&bitmap_offset, sizeof(bitmap_offset), 1, in_file) != 1) {
            LOGD("read bmp file header error\n");
            fclose(in_file);
            return 0;
        }
        if (fread(&header_size, sizeof(header_size), 1, in_file) != 1) {
            LOGD("read bmp file header error\n");
            fclose(in_file);
            return 0;
        }
        if (fread(&width, sizeof(width), 1, in_file) != 1) {
            LOGD("read bmp file header error\n");
            fclose(in_file);
            return 0;
        }
        if (fread(&height, sizeof(height), 1, in_file) != 1) {
            LOGD("read bmp file header error\n");
            fclose(in_file);
            return 0;
        }
        fseek(in_file, 2L, SEEK_CUR);
        if (fread(&bits_per_pixel, sizeof(bits_per_pixel), 1, in_file) != 1) {
            LOGD("read bmp file header error\n");
            fclose(in_file);
            return 0;
        }
        if (fread(&compression, sizeof(compression), 1, in_file) != 1) {
            LOGD("read bmp file header error\n");
            fclose(in_file);
            return 0;
        }
        if (fread(&bitmap_size, sizeof(bitmap_size), 1, in_file) != 1) {
            LOGD("read bmp file header error\n");
            fclose(in_file);
            return 0;
        }
        fseek(in_file, 8L, SEEK_CUR);
        if (fread(&num_colors, sizeof(num_colors), 1, in_file) != 1) {
            LOGD("read bmp file header error\n");
            fclose(in_file);
            return 0;
        }

        // 暂时只支持不压缩的格式，不支持RLE4和RLE8压缩格式（目前bmp图片基本都不压缩，需要再看情况适配）
        if (compression != 0) {
            LOGD("can't support RLE4 or RLE8 compression at present...");
            fclose(in_file);
            return 0;
        }

        int stride;
        int channels;
        // 暂时只支持24位图和32位图。。。呃。。。看情况再增加吧。。
        if (bits_per_pixel == 24) {
            channels = 3;
            stride = (width * 24 + 31) / 32 * 4;
        } else if (bits_per_pixel == 32) {
            channels = 4;
            stride = width * 4;
        } else {
            LOGD("only support 24bits or 32bits per pixel yet...");
            fclose(in_file);
            return 0;
        }

        // 根据旋转角度映射剪切位置
        calculate_crop_area(width, height, &x, &y, &w, &h, rotate);

        int out_width = w;
        int out_height = h;
        int out_stride;
        if (compress_method) {
            compress_method(w, h, &out_width, &out_height, min_width);
        }
        out_stride = out_width * channels * sizeof(unsigned char);

        // 读取图片数据，如需缩小，则缩小
        unsigned char *pixels = (unsigned char *) malloc(
                out_height * out_stride);
        if (!pixels) {
            LOGD("out of memory when load bmp data..");
            fclose(in_file);
            return NULL;
        }

        unsigned char *line;
        if (out_width == w) {
            // 移动文件指针到数据开始处（bmp顺序为下到上，左到右）
            // 忽略前(height - y - h)行
            fseek(in_file, bitmap_offset + (height - y - h) * stride, SEEK_SET);

            for (i = 0; i < out_height; i++) {
                line = &pixels[(out_height - 1 - i) * out_stride];
                // 忽略前x * channels个字节
                fseek(in_file, x * channels, SEEK_CUR);
                for (j = 0; j < out_width; j++, line += channels) {
                    if (fread(line + 2, 1, 1, in_file) != 1) {
                        LOGD("read bmp image data error\n");
                        fclose(in_file);
                        free(pixels);
                        return 0;
                    }
                    if (fread(line + 1, 1, 1, in_file) != 1) {
                        LOGD("read bmp image data error\n");
                        fclose(in_file);
                        free(pixels);
                        return 0;
                    }
                    if (fread(line, 1, 1, in_file) != 1) {
                        LOGD("read bmp image data error\n");
                        fclose(in_file);
                        free(pixels);
                        return 0;
                    }
                    if (channels == 4) {
                        if (fread(line + 3, 1, 1, in_file) != 1) {
                            LOGD("read bmp image data error\n");
                            fclose(in_file);
                            free(pixels);
                            return 0;
                        }
                    }
                }
                // 忽略后面(width - x - w)个字节
                fseek(in_file, (width - x - w) * channels, SEEK_CUR);
                // 忽略后面补齐的字节
                fseek(in_file, (stride - width * channels), SEEK_CUR);
            }
        } else {
            float scale = out_width / (float) w;

            int area_stride = w * channels * sizeof(unsigned char);
            unsigned char *base_line_pointer = (unsigned char *) malloc(
                    area_stride);
            unsigned char *next_line_pointer = (unsigned char *) malloc(
                    area_stride);
            unsigned char *out_line_pointer;

            int up_left, up_right, down_left, down_right;
            float fX, fY;
            int iX, iY;
            int c;
            // current_line表示已经读取过的行数
            // iY表示此线性差值需要使用第iY+1和iY+2行数据（因为iY从0开始）
            int current_line = 0;
            for (i = 0; i < out_height; i++) {
                out_line_pointer = &pixels[i * out_stride];

                fY = (float) (i + 1) / scale - 1;
                iY = (int) fY;

                // 要忽略y行，所以实际的行为(计算的行-y)
                // 然后每行要先忽略 x*channels个字节
                if (iY != h - 1) {
                    if (current_line <= iY) {
                        current_line += (iY - current_line);
                        // 将要读取第current+1行数据，因为是倒叙，所以对应第height - (current_line + 1)行，然后还需要算上忽略的y行
                        fseek(in_file,
                                bitmap_offset
                                        + (height - 1 - current_line - y)
                                                * stride + x * channels,
                                SEEK_SET);
                        if (fread(base_line_pointer, area_stride, 1, in_file)
                                != 1) {
                            LOGD("read bmp image data error...");
                            free(base_line_pointer);
                            free(next_line_pointer);
                            fclose(in_file);
                            free(pixels);
                            return NULL;
                        }
                        current_line++;

                        fseek(in_file,
                                bitmap_offset
                                        + (height - 1 - current_line - y)
                                                * stride + x * channels,
                                SEEK_SET);
                        if (fread(next_line_pointer, area_stride, 1, in_file)
                                != 1) {
                            LOGD("read bmp image data error...");
                            free(base_line_pointer);
                            free(next_line_pointer);
                            fclose(in_file);
                            free(pixels);
                            return NULL;
                        }
                        current_line++;
                    } else if (current_line == (iY + 1)) {
                        memcpy(base_line_pointer, next_line_pointer,
                                area_stride);
                        fseek(in_file,
                                bitmap_offset
                                        + (height - 1 - current_line - y)
                                                * stride + x * channels,
                                SEEK_SET);
                        if (fread(next_line_pointer, area_stride, 1, in_file)
                                != 1) {
                            LOGD("read bmp image data error...");
                            free(base_line_pointer);
                            free(next_line_pointer);
                            fclose(in_file);
                            free(pixels);
                            return NULL;
                        }
                        current_line++;
                    }
                } else {
                    current_line += (iY - current_line);

                    fseek(in_file,
                            bitmap_offset
                                    + (height - 1 - current_line - y) * stride
                                    + x * channels, SEEK_SET);
                    if (fread(base_line_pointer, area_stride, 1, in_file)
                            != 1) {
                        LOGD("read bmp image data error...");
                        free(base_line_pointer);
                        free(next_line_pointer);
                        fclose(in_file);
                        free(pixels);
                        return NULL;
                    }
                    current_line++;
                    memcpy(next_line_pointer, base_line_pointer, area_stride);
                }

                switch (channels) {
                case 3:
                    for (j = 0; j < out_width;
                            j++, out_line_pointer += channels) {
                        fX = (float) (j + 1) / scale - 1;
                        iX = (int) fX;

                        for (c = 0; c < channels; c++) {
                            up_left = base_line_pointer[iX * channels + c];
                            up_right =
                                    base_line_pointer[(iX + 1) * channels + c];
                            down_left = next_line_pointer[iX * channels + c];
                            down_right = next_line_pointer[(iX + 1) * channels
                                    + c];

                            out_line_pointer[channels - 1 - c] =
                                    CLAMP((int) (up_left * (iX + 1 - fX) * (iY + 1 - fY)
                                                    + up_right * (fX - iX) * (iY + 1 - fY)
                                                    + down_left * (iX + 1 - fX) * (fY - iY)
                                                    + down_right * (fX - iX) * (fY - iY)));
                        }
                    }
                    break;
                case 4:
                    for (j = 0; j < out_width;
                            j++, out_line_pointer += channels) {
                        fX = (float) (j + 1) / scale - 1;
                        iX = (int) fX;

                        for (c = 0; c < 3; c++) {
                            up_left = base_line_pointer[iX * channels + c];
                            up_right =
                                    base_line_pointer[(iX + 1) * channels + c];
                            down_left = next_line_pointer[iX * channels + c];
                            down_right = next_line_pointer[(iX + 1) * channels
                                    + c];

                            out_line_pointer[2 - c] =
                                    CLAMP((int) (up_left * (iX + 1 - fX) * (iY + 1 - fY)
                                                    + up_right * (fX - iX) * (iY + 1 - fY)
                                                    + down_left * (iX + 1 - fX) * (fY - iY)
                                                    + down_right * (fX - iX) * (fY - iY)));
                        }

                        up_left = base_line_pointer[iX * channels + 3];
                        up_right = base_line_pointer[(iX + 1) * channels + 3];
                        down_left = next_line_pointer[iX * channels + 3];
                        down_right = next_line_pointer[(iX + 1) * channels + 3];
                        out_line_pointer[3] =
                                CLAMP((int) (up_left * (iX + 1 - fX) * (iY + 1 - fY)
                                                + up_right * (fX - iX) * (iY + 1 - fY)
                                                + down_left * (iX + 1 - fX) * (fY - iY)
                                                + down_right * (fX - iX) * (fY - iY)));
                    }
                    break;
                default:
                    LOGD("read bmp image data error\n");
                    free(base_line_pointer);
                    free(next_line_pointer);
                    fclose(in_file);
                    free(pixels);
                    return NULL;
                }
            }

            free(base_line_pointer);
            free(next_line_pointer);
        }

        fclose(in_file);

        data = init_rrimage();
        data->width = out_width;
        data->height = out_height;
        data->channels = channels;
        data->stride = out_stride;
        data->type = TYPE_RRIMAGE_BMP;
        data->pixels = pixels;

        // 旋转处理
        flip_or_rotate(data, rotate);
    } else if (file_type == TYPE_RRIMAGE_GIF) {
        fclose(in_file);
        data = read_gif(file_name);

        int width = data->width;
        int height = data->height;
        // GRAY将被转换为RGB
        int channels = data->channels;

        calculate_crop_area(width, height, &x, &y, &w, &h, rotate);

        int out_width = w;
        int out_height = h;
        int out_stride;
        // 计算裁剪后的图片压缩后的宽高
        if (compress_method) {
            compress_method(w, h, &out_width, &out_height, min_width);
        }
        out_stride = out_width * channels * sizeof(unsigned char);
        int stride = width * channels * sizeof(unsigned char);

        // 指向最终输出的图片全部数据
        unsigned char *pixels = (unsigned char *) malloc(
                out_height * out_stride);
        unsigned char *src = data->pixels;
        if (!pixels) {
            LOGD("out of memory when read png file...");
            return NULL;
        }

        if (out_width == w) {
            // 指向原图中一行数据
            unsigned char *in_line_pointer = (unsigned char *) malloc(stride);

            for (i = 0; i < height; i++) {
                memcpy(in_line_pointer, &src[i * stride], stride * sizeof(unsigned char));

                if (i < y) {
                    continue;
                }

                if (i > y + h - 1) {
                    break;
                }

                memcpy(&pixels[(i - y) * out_stride],
                        in_line_pointer + x * channels, out_stride);
            }
            free(in_line_pointer);
        } else {
            float scale = out_width / (float) w;

            int area_stride = w * channels * sizeof(unsigned char);
            unsigned char *base_line_pointer = (unsigned char *) malloc(
                    area_stride);
            unsigned char *next_line_pointer = (unsigned char *) malloc(
                    area_stride);
            unsigned char *out_line_pointer;

            int up_left, up_right, down_left, down_right;
            float fX, fY;
            int iX, iY;
            int c;
            // current_line表示已经读取过的行数
            // iY表示此线性差值需要使用第iY+1和iY+2行数据（因为iY从0开始）
            int current_line = 0;
            for (i = 0; i < out_height; i++) {
                out_line_pointer = &pixels[i * out_stride];

                fY = (float) (i + 1) / scale - 1;
                iY = (int) fY;

                // 要忽略y行，所以实际的行为(计算的行-y)
                // 然后每行要先忽略 x*channels个字节
                if (iY != h - 1) {
                    if (current_line <= iY) {
                        current_line += (iY - current_line);
                        // 将要读取第current+1行数据，然后还需要算上忽略的y行
                        memcpy(base_line_pointer, &src[(y + current_line) * stride + x * channels], area_stride);
                        current_line++;

                        memcpy(next_line_pointer, &src[(y + current_line) * stride + x * channels], area_stride);
                        current_line++;
                    } else if (current_line == (iY + 1)) {
                        memcpy(base_line_pointer, next_line_pointer,
                                area_stride);
                        memcpy(next_line_pointer, &src[(y + current_line) * stride + x * channels], area_stride);
                        current_line++;
                    }
                } else {
                    current_line += (iY - current_line);

                    memcpy(base_line_pointer, &src[(y + current_line) * stride + x * channels], stride);
                    current_line++;
                    memcpy(next_line_pointer, base_line_pointer, area_stride);
                }

                switch (channels) {
                case 3:
                    for (j = 0; j < out_width;
                            j++, out_line_pointer += channels) {
                        fX = (float) (j + 1) / scale - 1;
                        iX = (int) fX;

                        for (c = 0; c < channels; c++) {
                            up_left = base_line_pointer[iX * channels + c];
                            up_right =
                                    base_line_pointer[(iX + 1) * channels + c];
                            down_left = next_line_pointer[iX * channels + c];
                            down_right = next_line_pointer[(iX + 1) * channels
                                    + c];

                            out_line_pointer[channels - 1 - c] =
                                    CLAMP((int) (up_left * (iX + 1 - fX) * (iY + 1 - fY)
                                                    + up_right * (fX - iX) * (iY + 1 - fY)
                                                    + down_left * (iX + 1 - fX) * (fY - iY)
                                                    + down_right * (fX - iX) * (fY - iY)));
                        }
                    }
                    break;
                case 4:
                    for (j = 0; j < out_width;
                            j++, out_line_pointer += channels) {
                        fX = (float) (j + 1) / scale - 1;
                        iX = (int) fX;

                        for (c = 0; c < 3; c++) {
                            up_left = base_line_pointer[iX * channels + c];
                            up_right =
                                    base_line_pointer[(iX + 1) * channels + c];
                            down_left = next_line_pointer[iX * channels + c];
                            down_right = next_line_pointer[(iX + 1) * channels
                                    + c];

                            out_line_pointer[2 - c] =
                                    CLAMP((int) (up_left * (iX + 1 - fX) * (iY + 1 - fY)
                                                    + up_right * (fX - iX) * (iY + 1 - fY)
                                                    + down_left * (iX + 1 - fX) * (fY - iY)
                                                    + down_right * (fX - iX) * (fY - iY)));
                        }

                        up_left = base_line_pointer[iX * channels + 3];
                        up_right = base_line_pointer[(iX + 1) * channels + 3];
                        down_left = next_line_pointer[iX * channels + 3];
                        down_right = next_line_pointer[(iX + 1) * channels + 3];
                        out_line_pointer[3] =
                                CLAMP((int) (up_left * (iX + 1 - fX) * (iY + 1 - fY)
                                                + up_right * (fX - iX) * (iY + 1 - fY)
                                                + down_left * (iX + 1 - fX) * (fY - iY)
                                                + down_right * (fX - iX) * (fY - iY)));
                    }
                    break;
                default:
                    LOGD("compress git image error\n");
                    free(base_line_pointer);
                    free(next_line_pointer);
                    free(pixels);
                    return NULL;
                }
            }

            free(base_line_pointer);
            free(next_line_pointer);
        }

        free(src);
        data->width = out_width;
        data->height = out_height;
        data->channels = channels;
        data->stride = out_stride;
        data->type = TYPE_RRIMAGE_GIF;
        data->pixels = pixels;

        // 旋转处理
        flip_or_rotate(data, rotate);
    } else {
        LOGD("file format not supported yet...");
        fclose(in_file);
    }

    return data;
}

int write_image(const char *file_name, rrimage *data) {
    /*
     int result;

     switch (data->type) {
     case TYPE_RRIMAGE_JPEG:
     result = write_jpeg(file_name, data);
     break;
     case TYPE_RRIMAGE_PNG:
     result = write_png(file_name, data);
     break;
     default:
     result = write_jpeg(file_name, data);
     break;
     }

     return result;
     */

    //无论jpeg和png或bmp格式，统一使用jpeg格式输出，quality为80
    data->quality = 80;
    return write_jpeg(file_name, data);
}

void compress_strategy(int width, int height, int *out_width, int *out_height,
        int min_width) {
    if (width > height) {
        *out_height = (height > min_width) ? min_width : height;
        *out_width = width * (*out_height) / height;
    } else {
        *out_width = (width > min_width) ? min_width : width;
        *out_height = height * (*out_width) / width;
    }

    long max_memory = 12 * 1024 * 1024 / 4;
    long image_memory = (*out_width) * (*out_height);
    if (image_memory > max_memory) {
        float ratio = (float) max_memory / image_memory;
        *out_width = (int) ((*out_width) * ratio);
        *out_height = (int) ((*out_height) * ratio);
    }
}

void flip_or_rotate(rrimage *data, int orientation) {

    if (!data || !data->pixels || orientation == ROTATE_0) {
        return;
    }

    int width = data->width;
    int height = data->height;
    int channels = data->channels;
    int stride = data->stride;

    int i, j, k;
    rrimage *result = clone_rrimage(data);

    unsigned char *sptr;
    unsigned char *dptr;

    unsigned char temp;

    switch (orientation) {
    case ROTATE_0:
        break;
    case FLIP_ROTATE_0:
        for (i = 0; i < height; i++) {
            dptr = &data->pixels[i * stride];
            sptr = &result->pixels[(i + 1) * stride - channels];

            for (j = 0; j < width; j++) {
                for (k = 0; k < channels; k++) {
                    *dptr++ = *sptr++;
                }
                sptr -= 2 * channels;
            }
        }
        break;
    case ROTATE_180:
        for (i = 0; i < height; i++) {
            dptr = &data->pixels[i * stride];
            sptr = &result->pixels[(height - i) * stride - channels];

            for (j = 0; j < width; j++) {
                for (k = 0; k < channels; k++) {
                    *dptr++ = *sptr++;
                }
                sptr -= 2 * channels;
            }
        }
        break;
    case FLIP_ROTATE_180:
        for (i = 0; i < height; i++) {
            dptr = &data->pixels[i * stride];
            sptr = &result->pixels[(height - 1 - i) * stride];

            for (j = 0; j < width; j++) {
                for (k = 0; k < channels; k++) {
                    *dptr++ = *sptr++;
                }
            }
        }
        break;
    case FLIP_ROTATE_90:
        data->width = height;
        data->height = width;
        data->stride = height * channels;

        sptr = result->pixels;
        dptr = data->pixels;
        for (i = 0; i < data->height; i++) {
            for (j = 0; j < data->width; j++) {
                for (k = 0; k < channels; k++) {
                    dptr[i * data->stride + j * channels + k] = sptr[j * stride
                            + i * channels + k];
                }
            }
        }
        break;
    case ROTATE_90:
        data->width = height;
        data->height = width;
        data->stride = height * channels;

        sptr = result->pixels;
        dptr = data->pixels;
        for (i = 0; i < data->height; i++) {
            for (j = 0; j < data->width; j++) {
                for (k = 0; k < channels; k++) {
                    dptr[i * data->stride + j * channels + k] = sptr[(height - 1
                            - j) * stride + i * channels + k];
                }
            }
        }
        break;
    case FLIP_ROTATE_270:
        data->width = height;
        data->height = width;
        data->stride = height * channels;

        sptr = result->pixels;
        dptr = data->pixels;
        for (i = 0; i < data->height; i++) {
            for (j = 0; j < data->width; j++) {
                for (k = 0; k < channels; k++) {
                    dptr[i * data->stride + j * channels + k] = sptr[(height - 1
                            - j) * stride + (width - 1 - i) * channels + k];
                }
            }
        }
        break;
    case ROTATE_270:
        data->width = height;
        data->height = width;
        data->stride = height * channels;

        sptr = result->pixels;
        dptr = data->pixels;
        for (i = 0; i < data->height; i++) {
            for (j = 0; j < data->width; j++) {
                for (k = 0; k < channels; k++) {
                    dptr[i * data->stride + j * channels + k] = sptr[j * stride
                            + (width - 1 - i) * channels + k];
                }
            }
        }
        break;
    default:
        break;
    }

    free_rrimage(result);
}
