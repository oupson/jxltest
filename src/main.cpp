#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <istream>

#include <fmt/core.h>

#include <jxl/decode.h>
#include <jxl/decode_cxx.h>
#include <jxl/resizable_parallel_runner_cxx.h>

#include <png.h>

#include <skcms.h>

class ImageOut {
  public:
    uint8_t *buffer;

    skcms_ICCProfile icc;
    uint8_t *icc_buffer;

    bool has_alpha;
    bool is_premultiplied;

    size_t width;
    size_t height;

    ImageOut(size_t width, size_t height, bool has_alpha, bool is_premultiplied)
        : width(width), height(height), has_alpha(has_alpha),
          is_premultiplied(is_premultiplied) {
        this->buffer =
            (uint8_t *)malloc(width * height * ((has_alpha) ? 4 : 3));
        this->icc_buffer = nullptr;
    }

    ~ImageOut() {
        if (this->buffer != nullptr) {
            free(this->buffer);
        }

        if (this->icc_buffer != nullptr) {
            free(this->icc_buffer);
        }
    }

    inline int parse_icc_profile(JxlDecoder *dec,
                                 const JxlPixelFormat &format) {
        size_t icc_size;
        if (JXL_DEC_SUCCESS !=
            JxlDecoderGetICCProfileSize(
                dec, &format, JXL_COLOR_PROFILE_TARGET_DATA, &icc_size)) {
            std::cerr << "JxlDecoderGetICCProfileSize failed" << std::endl;
            return -1;
        }

        if (!(this->icc_buffer = (uint8_t *)malloc(icc_size))) {
            std::cerr << "Allocating ICC profile failed" << std::endl;
            return -1;
        }

        if (JXL_DEC_SUCCESS != JxlDecoderGetColorAsICCProfile(
                                   dec, &format, JXL_COLOR_PROFILE_TARGET_DATA,
                                   this->icc_buffer, icc_size)) {
            std::cerr << "JxlDecoderGetColorAsICCProfile failed" << std::endl;
            return -1;
        }

        if (!skcms_Parse((void *)this->icc_buffer, icc_size, &icc)) {
            std::cerr << "Invalid ICC profile from JXL image decoder"
                      << std::endl;
            return -1;
        }
        return 0;
    }
};

void image_out_callback(void *opaque_data, size_t x, size_t y,
                        size_t num_pixels, const void *pixels) {
    ImageOut *data = (ImageOut *)opaque_data;

    skcms_Transform(
        pixels, skcms_PixelFormat_RGBA_8888,
        data->is_premultiplied ? skcms_AlphaFormat_PremulAsEncoded
                               : skcms_AlphaFormat_Unpremul,
        &data->icc,
        data->buffer + ((y * data->width + x) * ((data->has_alpha) ? 4 : 3)),
        data->has_alpha ? skcms_PixelFormat_RGBA_8888
                        : skcms_PixelFormat_RGB_888,
        skcms_AlphaFormat_Unpremul, skcms_sRGB_profile(), num_pixels);
}

int main(int argc, char **argv) {
    const char *file_input_path;
    if (argc == 1) {
        file_input_path = "test.jxl";
    } else {
        file_input_path = argv[1];
    }

    auto file_input = std::ifstream(file_input_path, std::ifstream::in);
    if (file_input.fail()) {
        std::cerr << "Failed to open file : " << strerror(errno) << std::endl;
        return -1;
    }

    auto dec = JxlDecoderMake(nullptr);

    if (JXL_DEC_SUCCESS !=
        JxlDecoderSubscribeEvents(dec.get(),
                                  JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE |
                                      JXL_DEC_COLOR_ENCODING | JXL_DEC_FRAME)) {
        std::cerr << "JxlDecoderSubscribeEvents failed" << std::endl;
        return -1;
    }

    auto runner = JxlResizableParallelRunnerMake(nullptr);
    if (JXL_DEC_SUCCESS !=
        JxlDecoderSetParallelRunner(dec.get(), JxlResizableParallelRunner,
                                    runner.get())) {
        std::cerr << "JxlDecoderSetParallelRunner failed" << std::endl;
        return -1;
    }

    char data[1024];
    file_input.read(data, 1024);

    if (file_input.fail() && !file_input.eof()) {
        std::cerr << "Failed to read from file : " << strerror(errno)
                  << std::endl;
        return -1;
    }

    JxlPixelFormat format = {4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};

    if (JXL_DEC_SUCCESS !=
        JxlDecoderSetInput(dec.get(), (uint8_t *)data, file_input.gcount())) {
        std::cerr << "JxlDecoderSetInput failed" << std::endl;
        return -1;
    }

    int index_image = 0;

    ImageOut *out;

    for (;;) {
        JxlDecoderStatus status = JxlDecoderProcessInput(dec.get());

        if (status == JXL_DEC_ERROR) {
            std::cerr << "Decoder Error" << std::endl;
            return -1;
        } else if (status == JXL_DEC_NEED_MORE_INPUT) {
            JxlDecoderReleaseInput(dec.get());
            file_input.read(data, 1024);

            if (file_input.fail() && !file_input.eof()) {
                std::cerr << "Failed to read from file : " << strerror(errno)
                          << std::endl;
                return -1;
            }

            if (JXL_DEC_SUCCESS != JxlDecoderSetInput(dec.get(),
                                                      (uint8_t *)data,
                                                      file_input.gcount())) {
                std::cerr << "JxlDecoderSetInput failed" << std::endl;
                return -1;
            }
        } else if (status == JXL_DEC_FRAME) {
            std::cout << "dec frame" << std::endl;

            if (JXL_DEC_SUCCESS !=
                JxlDecoderSetImageOutCallback(dec.get(), &format,
                                              image_out_callback, out)) {
                std::cerr << "JxlDecoderSetImageOutCallback failed"
                          << std::endl;
                return -1;
            }
        } else if (status == JXL_DEC_COLOR_ENCODING) {
            out->parse_icc_profile(dec.get(), format);
        } else if (status == JXL_DEC_BASIC_INFO) {
            JxlBasicInfo info;
            if (JXL_DEC_SUCCESS != JxlDecoderGetBasicInfo(dec.get(), &info)) {
                std::cerr << "JxlDecoderGetBasicInfo failed" << std::endl;
                return -1;
            }

            out = new ImageOut(info.xsize, info.ysize, info.alpha_bits > 0,
                               info.alpha_premultiplied);

            JxlResizableParallelRunnerSetThreads(
                runner.get(), JxlResizableParallelRunnerSuggestThreads(
                                  info.xsize, info.ysize));
        } else if (status == JXL_DEC_FULL_IMAGE) {
            auto out_path = fmt::format("{:02}.png", index_image);

            png_image image = {};
            image.width = out->width;
            image.height = out->height;
            image.version = PNG_IMAGE_VERSION;
            image.format = PNG_FORMAT_RGBA;

            png_image_write_to_file(&image, out_path.c_str(), 0, out->buffer, 0,
                                    nullptr);
            index_image += 1;
        } else if (status == JXL_DEC_SUCCESS) {
            std::cout << "success" << std::endl;
            break;
        } else {
            std::cerr << "Unknown status" << std::endl;
        }
    }

    delete out;
    return 0;
}
