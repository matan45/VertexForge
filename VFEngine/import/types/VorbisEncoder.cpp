#include "VorbisEncoder.hpp"
#include "print/Log.hpp"

#include <vorbis/vorbisenc.h>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace types
{
    float VorbisEncoder::qualityToFloat(int qualityEnum)
    {
        // Maps importConfig::AudioCompressionQuality values
        // Low=0, Medium=1, High=2, Lossless=3
        switch (qualityEnum)
        {
            case 0: return 0.1f;   // Low ~80kbps
            case 1: return 0.4f;   // Medium ~128kbps
            case 2: return 0.6f;   // High ~192kbps
            default: return 0.4f;
        }
    }

    std::vector<uint8_t> VorbisEncoder::encode(const short* pcm, size_t sampleCount,
                                                uint32_t channels, uint32_t sampleRate,
                                                float quality)
    {
        std::vector<uint8_t> result;

        if (!pcm || sampleCount == 0 || channels == 0 || sampleRate == 0)
        {
            vfLogError("VorbisEncoder: invalid input parameters");
            return result;
        }

        vorbis_info vi;
        vorbis_info_init(&vi);

        int ret = vorbis_encode_init_vbr(&vi, static_cast<long>(channels),
                                          static_cast<long>(sampleRate), quality);
        if (ret != 0)
        {
            vfLogError("VorbisEncoder: vorbis_encode_init_vbr failed ({})", ret);
            vorbis_info_clear(&vi);
            return result;
        }

        vorbis_comment vc;
        vorbis_comment_init(&vc);
        vorbis_comment_add_tag(&vc, "ENCODER", "VertexForge");

        vorbis_dsp_state vd;
        vorbis_block vb;
        ogg_stream_state os;
        ogg_page og;
        ogg_packet op;

        vorbis_analysis_init(&vd, &vi);
        vorbis_block_init(&vd, &vb);

        srand(static_cast<unsigned>(time(nullptr)));
        ogg_stream_init(&os, rand());

        // Write Vorbis headers
        ogg_packet header;
        ogg_packet header_comm;
        ogg_packet header_code;

        vorbis_analysis_headerout(&vd, &vc, &header, &header_comm, &header_code);
        ogg_stream_packetin(&os, &header);
        ogg_stream_packetin(&os, &header_comm);
        ogg_stream_packetin(&os, &header_code);

        // Flush header pages
        while (ogg_stream_flush(&os, &og))
        {
            result.insert(result.end(),
                          reinterpret_cast<uint8_t*>(og.header),
                          reinterpret_cast<uint8_t*>(og.header) + og.header_len);
            result.insert(result.end(),
                          reinterpret_cast<uint8_t*>(og.body),
                          reinterpret_cast<uint8_t*>(og.body) + og.body_len);
        }

        // Encode PCM data in chunks
        const size_t framesPerChunk = 1024;
        size_t totalFrames = sampleCount / channels;
        size_t framesProcessed = 0;
        bool eos = false;

        while (!eos)
        {
            size_t framesToProcess = std::min(framesPerChunk, totalFrames - framesProcessed);

            if (framesToProcess == 0)
            {
                // Signal end of stream
                vorbis_analysis_wrote(&vd, 0);
            }
            else
            {
                // Get buffer from vorbis and fill with float samples
                float** buffer = vorbis_analysis_buffer(&vd, static_cast<int>(framesToProcess));

                for (size_t i = 0; i < framesToProcess; ++i)
                {
                    for (uint32_t ch = 0; ch < channels; ++ch)
                    {
                        size_t sampleIdx = (framesProcessed + i) * channels + ch;
                        buffer[ch][i] = pcm[sampleIdx] / 32768.0f;
                    }
                }

                vorbis_analysis_wrote(&vd, static_cast<int>(framesToProcess));
                framesProcessed += framesToProcess;
            }

            // Extract encoded packets
            while (vorbis_analysis_blockout(&vd, &vb) == 1)
            {
                vorbis_analysis(&vb, nullptr);
                vorbis_bitrate_addblock(&vb);

                while (vorbis_bitrate_flushpacket(&vd, &op))
                {
                    ogg_stream_packetin(&os, &op);

                    while (!eos)
                    {
                        if (ogg_stream_pageout(&os, &og) == 0)
                            break;

                        result.insert(result.end(),
                                      reinterpret_cast<uint8_t*>(og.header),
                                      reinterpret_cast<uint8_t*>(og.header) + og.header_len);
                        result.insert(result.end(),
                                      reinterpret_cast<uint8_t*>(og.body),
                                      reinterpret_cast<uint8_t*>(og.body) + og.body_len);

                        if (ogg_page_eos(&og))
                            eos = true;
                    }
                }
            }
        }

        // Cleanup
        ogg_stream_clear(&os);
        vorbis_block_clear(&vb);
        vorbis_dsp_clear(&vd);
        vorbis_comment_clear(&vc);
        vorbis_info_clear(&vi);

        return result;
    }
}
