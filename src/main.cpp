#include "jxl/resizable_parallel_runner.h"
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

#include <Magick++.h>

#include <skcms.h>

class ImageOut {
  public:
    uint8_t *buffer;
    skcms_ICCProfile *icc;
    bool has_alpha;
    bool premul;
    int index;
    int width;
    int height;

    ImageOut(uint8_t *buffer, int index, int width, int height, bool has_alpha,
             bool premul, skcms_ICCProfile *icc)
        : buffer(buffer), index(index), width(width), height(height),
          has_alpha(has_alpha), premul(premul), icc(icc) {}
};

void image_out_callback(void *opaque_data, size_t x, size_t y,
                        size_t num_pixels, const void *pixels) {
    ImageOut *data = (ImageOut *)opaque_data;

    skcms_Transform(
        pixels, skcms_PixelFormat_RGBA_8888,
        data->premul ? skcms_AlphaFormat_PremulAsEncoded
                     : skcms_AlphaFormat_Unpremul,
        data->icc,
        data->buffer + ((y * data->width + x) * ((data->has_alpha) ? 4 : 3)),
        data->has_alpha ? skcms_PixelFormat_RGBA_8888
                        : skcms_PixelFormat_RGB_888,
        skcms_AlphaFormat_Unpremul, skcms_sRGB_profile(), num_pixels);
}

int main(void) {
    auto file_input = std::ifstream("test.jxl", std::ifstream::in);
    if (file_input.fail()) {
        std::cerr << "Failed to open file" << std::endl;
        std::exit(-1);
    }

    auto dec = JxlDecoderMake(nullptr);

    if (JXL_DEC_SUCCESS !=
        JxlDecoderSubscribeEvents(dec.get(),
                                  JXL_DEC_BASIC_INFO | JXL_DEC_FULL_IMAGE |
                                      JXL_DEC_COLOR_ENCODING | JXL_DEC_FRAME)) {
        std::exit(-1);
    }

    auto runner = JxlResizableParallelRunnerMake(nullptr);
    if (JXL_DEC_SUCCESS !=
        JxlDecoderSetParallelRunner(dec.get(), JxlResizableParallelRunner,
                                    runner.get())) {
        std::exit(-1);
    }

    char data[1024];
    file_input.read(data, 1024);

    JxlPixelFormat format = {4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};

    JxlDecoderSetInput(dec.get(), (uint8_t *)data, 1024);

    int xsize = 0;
    int ysize = 0;

    int index_image = 0;

    bool premul = false;
    bool alpha = false;
    skcms_ICCProfile icc;
    ImageOut *out = nullptr;
    uint8_t *icc_buff;
    for (;;) {
        JxlDecoderStatus status = JxlDecoderProcessInput(dec.get());

        if (status == JXL_DEC_ERROR) {
            std::cerr << "Decoder Error" << std::endl;
        } else if (status == JXL_DEC_NEED_MORE_INPUT) {
            JxlDecoderReleaseInput(dec.get());
            file_input.read(data, 1024);

            JxlDecoderSetInput(dec.get(), (uint8_t *)data, file_input.gcount());
        } else if (status == JXL_DEC_FRAME) {
            std::cout << "dec frame" << std::endl;

            uint8_t *buffer_out = (uint8_t *)malloc(((alpha) ? 4 : 3) * xsize *
                                                    ysize * sizeof(uint8_t));

            out = new ImageOut(buffer_out, index_image, xsize, ysize, alpha,
                               premul, &icc);

            if (JXL_DEC_SUCCESS !=
                JxlDecoderSetImageOutCallback(dec.get(), &format,
                                              image_out_callback, out)) {
                std::cerr << "JxlDecoderSetImageOutCallback failed"
                          << std::endl;
                return -1;
            }

            index_image += 1;
        } else if (status == JXL_DEC_COLOR_ENCODING) {
            size_t icc_size;
            if (JXL_DEC_SUCCESS !=
                JxlDecoderGetICCProfileSize(dec.get(), &format,
                                            JXL_COLOR_PROFILE_TARGET_DATA,
                                            &icc_size)) {
                std::cerr << "JxlDecoderGetICCProfileSize failed" << std::endl;
                return -1;
            }

            if (!(icc_buff = (uint8_t *)malloc(icc_size))) {
                std::cerr << "Allocating ICC profile failed" << std::endl;
                return -1;
            }

            if (JXL_DEC_SUCCESS !=
                JxlDecoderGetColorAsICCProfile(dec.get(), &format,
                                               JXL_COLOR_PROFILE_TARGET_DATA,
                                               icc_buff, icc_size)) {
                std::cerr << "JxlDecoderGetColorAsICCProfile failed"
                          << std::endl;
                return -1;
            }

            if (!skcms_Parse((void *)icc_buff, icc_size, &icc)) {
                std::cerr << "Invalid ICC profile from JXL image decoder"
                          << std::endl;
                return -1;
            }
        } else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
            std::exit(-1);
        } else if (status == JXL_DEC_BASIC_INFO) {
            JxlBasicInfo info;
            if (JXL_DEC_SUCCESS != JxlDecoderGetBasicInfo(dec.get(), &info)) {
                std::cerr << "JxlDecoderGetBasicInfo failed" << std::endl;
                return -1;
            }
            xsize = info.xsize;
            ysize = info.ysize;

            alpha = info.alpha_bits > 0;
            premul = info.alpha_premultiplied;

            std::cout << fmt::format("has alpha : {}, premul : {}", alpha,
                                     premul)
                      << std::endl;

            JxlResizableParallelRunnerSetThreads(
                runner.get(), JxlResizableParallelRunnerSuggestThreads(
                                  info.xsize, info.ysize));
        } else if (status == JXL_DEC_FULL_IMAGE) {
            auto out_path = fmt::format("{:02}.png", out->index);

            Magick::Image image(xsize, ysize, (out->has_alpha) ? "RGBA" : "RGB",
                                Magick::StorageType::CharPixel, out->buffer);
            image.write(out_path);
            std::cout << fmt::format("Saved frame {} to disk", out->index)
                      << std::endl;
            delete out;
        } else if (status == JXL_DEC_SUCCESS) {
            std::cout << "success" << std::endl;
            break;
        } else {
            std::cerr << "Unknown status" << std::endl;
        }
    }

    return 0;
}
