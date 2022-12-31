#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <istream>

#include <fmt/core.h>

#include <jxl/decode.h>
#include <jxl/decode_cxx.h>

#include <Magick++.h>

int main(void) {
    std::cout << "Hello, World !" << std::endl;

    auto file_input = std::ifstream("test.jxl", std::ifstream::in);
    if (file_input.fail()) {
        std::cerr << "Failed to open file" << std::endl;
        std::exit(-1);
    }

    auto dec = JxlDecoderMake(nullptr);

    if (JXL_DEC_SUCCESS !=
        JxlDecoderSubscribeEvents(dec.get(), JXL_DEC_BASIC_INFO |
                                                 JXL_DEC_FULL_IMAGE |
                                                 JXL_DEC_FRAME)) {
        std::exit(-1);
    }

    char data[1024];
    file_input.read(data, 1024);

    JxlPixelFormat format = {4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0};

    JxlDecoderSetInput(dec.get(), (uint8_t *)data, 1024);

    int xsize = 0;
    int ysize = 0;

    uint8_t *buffer_out = nullptr;
    size_t buffer_size = 0;

    int index_image = 0;

    for (;;) {
        JxlDecoderStatus status = JxlDecoderProcessInput(dec.get());

        if (status == JXL_DEC_ERROR) {
            std::cerr << "Decoder Error" << std::endl;
        } else if (status == JXL_DEC_NEED_MORE_INPUT) {
            JxlDecoderReleaseInput(dec.get());
            file_input.read(data, 1024);

            JxlDecoderSetInput(dec.get(), (uint8_t *)data, file_input.gcount());
            std::cerr << "Need more input" << std::endl;
        } else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
            std::cout << "need out buffer" << std::endl;
            size_t new_buffer_size;
            if (JXL_DEC_SUCCESS != JxlDecoderImageOutBufferSize(
                                       dec.get(), &format, &new_buffer_size)) {
                std::cerr << "JxlDecoderImageOutBufferSize failed" << std::endl;
                return -1;
            }

            if (new_buffer_size != xsize * ysize * 4) {
                std::cerr << "Invalid out buffer size "
                          << static_cast<uint64_t>(new_buffer_size) << ", want "
                          << static_cast<uint64_t>(xsize * ysize * 4)
                          << std::endl;
                return -1;
            }

            if (buffer_size != new_buffer_size) {
                buffer_out = (uint8_t *)realloc(
                    buffer_out, sizeof(uint8_t) * new_buffer_size);
                buffer_size = new_buffer_size;
            }

            if (JXL_DEC_SUCCESS !=
                JxlDecoderSetImageOutBuffer(dec.get(), &format, buffer_out,
                                            buffer_size * sizeof(uint8_t))) {
                std::cerr << "JxlDecoderSetImageOutBuffer failed" << std::endl;
                return -1;
            }
        } else if (status == JXL_DEC_BASIC_INFO) {
            JxlBasicInfo info;
            if (JXL_DEC_SUCCESS != JxlDecoderGetBasicInfo(dec.get(), &info)) {
                std::cerr << "JxlDecoderGetBasicInfo failed" << std::endl;
                return -1;
            }
            xsize = info.xsize;
            ysize = info.ysize;
        } else if (status == JXL_DEC_FULL_IMAGE) {
            std::cout << "dec full image" << std::endl;
            auto out_path = fmt::format("{}.png", index_image);
            index_image += 1;

            Magick::Image image(xsize, ysize, "RGBA",
                                Magick::StorageType::CharPixel, buffer_out);
            image.write(out_path);
        } else if (status == JXL_DEC_FRAME) {
            std::cout << "dec frame" << std::endl;
        } else if (status == JXL_DEC_SUCCESS) {
            std::cout << "success" << std::endl;
            break;
        } else {
            std::cerr << "Unknown status" << std::endl;
        }
    }

    free(buffer_out);

    return 0;
}
