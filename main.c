#include "rrimagelib.h"

int main(int argc, char *argv[]) {
    rrimage *data = read_image_with_compress_by_area("/Users/robert/Pictures/square.gif", compress_strategy, 960, 0, 0, 300, 300, ROTATE_90);
    printf("width = %d, height = %d, channels = %d\n", data->width, data->height, data->channels);
    printf("write result is: %d\n", write_image("/Users/robert/Desktop/out.jpg", data));

    return 0;
}
