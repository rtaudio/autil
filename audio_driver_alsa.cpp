#include "audio_driver_alsa.h"

#include <vector>
#include <fstream>
#include <string>
#include <string.h>
#include <iostream>
#include <exception>

#include "signal_buffer.h"

#include "test.h"

namespace autil {

/* we dont use MMAP, see
 * http://stackoverflow.com/questions/14762103/recording-from-alsa-understanding-memory-mapping
 */

int bits = 32;

int throwIfError(int err, const std::string &msg) {
    if(err < 0) {
        throw std::runtime_error(msg + " ("+std::string(snd_strerror(err))+")");
    }
}

int
configure_alsa_audio(snd_pcm_t *device, int channels, int buffer_size, int sample_rate)
{
    snd_pcm_hw_params_t *hw_params;
    int                 err;
    unsigned int                 tmp;
    snd_pcm_uframes_t   frames;
    unsigned int                 fragments = 2; // TODO: should be more for capture

    // TODO: free!

    /* allocate memory for hardware parameter structure */
    throwIfError(snd_pcm_hw_params_malloc(&hw_params),
                 "cannot allocate parameter structure");

    /* fill structure from current audio parameters */
    throwIfError(snd_pcm_hw_params_any(device, hw_params),
                 "cannot initialize parameter structure");

    /* set access type, sample rate, sample format, channels */
    throwIfError(snd_pcm_hw_params_set_access(device, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED),
                 "cannot set access type");

    // bits = 16
    throwIfError(snd_pcm_hw_params_set_format(device, hw_params, SND_PCM_FORMAT_S16_LE),
                 "cannot set sample format");

    // snd_pcm_hw_params_set_rate_resample: allow resampling

    tmp = sample_rate;
    throwIfError(snd_pcm_hw_params_set_rate_near(device, hw_params, &tmp, 0),
                 "cannot set sample rate");

    if (tmp != sample_rate) {
        fprintf(stderr, "Could not set requested sample rate, asked for %d got %d\n", sample_rate, tmp);
        sample_rate = tmp;
    }

    throwIfError(snd_pcm_hw_params_set_channels(device, hw_params, channels),
            "cannot set channel count");

    throwIfError(snd_pcm_hw_params_set_periods_near(device, hw_params, &fragments, 0),
                 "Error setting # fragments to " + std::to_string(fragments)
                  );

    int frame_size = channels * (bits / 8);
       frames = buffer_size / frame_size * fragments;
       throwIfError(snd_pcm_hw_params_set_buffer_size_near(device, hw_params, &frames),
                    "Error setting buffer_size " + std::to_string(frames) + " frames");

       if (buffer_size != frames * frame_size / fragments) {
           fprintf(stderr, "Could not set requested buffer size, asked for %d got %d\n", (int)buffer_size, (int)(frames * frame_size / fragments));
           buffer_size = frames * frame_size / fragments;
       }

       if ((err = snd_pcm_hw_params(device, hw_params)) < 0) {
           fprintf(stderr, "Error setting HW params: %s\n",
                   snd_strerror(err));
           return 1;
       }
       return 0;
   }


    AudioDriverAlsa::AudioDriverAlsa(const std::string &deviceName, const StreamProperties &props)
        : AudioDriverBase(deviceName)
	{
        throwIfError(snd_pcm_open(&playback_handle, deviceName.c_str(), SND_PCM_STREAM_PLAYBACK, 0),
                     "cannot open output audio device "+deviceName);

         throwIfError(snd_pcm_open(&capture_handle, deviceName.c_str(), SND_PCM_STREAM_CAPTURE, 0),
                 "cannot open input audio device "+deviceName);

         m_sampleRate = props.sampleRate;
         m_numChannelsCapture = props.numChannelsCapture;
         m_numChannelsPlayback = props.numChannelsPlayback;
        setBlockSize(props.blockSize);


		m_running = true;

        m_audioThread = new RttThread([this]() {
            process();
        }, true);
	}

    AudioDriverAlsa::~AudioDriverAlsa()
    {
        delete m_audioThread;

        if(playback_handle)
            snd_pcm_close(playback_handle);
        if(capture_handle)
            snd_pcm_close(capture_handle);
    }

    void AudioDriverAlsa::setBlockSize(int blockSize) {
        m_blockSize = blockSize;
        if(playback_handle)
            configure_alsa_audio(playback_handle, m_numChannelsPlayback, m_blockSize, m_sampleRate);
        if(capture_handle)
            configure_alsa_audio(capture_handle, m_numChannelsCapture, m_blockSize, m_sampleRate);
    }





    void AudioDriverAlsa::process()
    {
        m_numShortWrites = 0;
        m_numUnderruns = 0;
        m_numShortReads = 0;
        m_numOverruns = 0;


        int             blockSize, inframes, outframes, frame_size;
        bool restarting = true;
        std::vector<float> pcmIn, pcmOut;

        const unsigned int                 fragments = 2;

        while (m_running) {
            processActionQueueInAudioThread();

            if (restarting) {
                restarting = false;

                //frame_size = channels * (bits / 8);
                //numFrames = buffer_size / frame_size;

                blockSize = m_blockSize;

                pcmIn.resize(m_numChannelsCapture * blockSize);
                pcmOut.resize(m_numChannelsPlayback * blockSize);

                /* drop any output we might got and stop */
                snd_pcm_drop(capture_handle);
                snd_pcm_drop(playback_handle);
                /* prepare for use */
                snd_pcm_prepare(capture_handle);
                snd_pcm_prepare(playback_handle);

                /* fill the whole output buffer */
                // TODO: set zero pcm data
                for (int i = 0; i < fragments; i += 1)
                    snd_pcm_writei(playback_handle, pcmOut.data(), blockSize);
            }

            auto pcmInPtr = pcmIn.data();
            auto pcmOutPtr = pcmOut.data();

            // read capture data
            while ((inframes = snd_pcm_readi(capture_handle, pcmInPtr, blockSize)) < 0) {
                if (inframes == -EAGAIN)
                    continue;

                m_numOverruns++;
                restarting = true;
                snd_pcm_prepare(capture_handle);
            }

            if (inframes != blockSize) {
                m_numShortReads++;
                fprintf(stderr, "Short read from capture device: %d, expecting %d\n", inframes, blockSize);
                // todo: zero set buffer at [inframes...blockSize]
            }


            // signal buffers
            for (int ib = 0; ib < MAX_SIGNAL_BUFFERS; ib++) {
                SignalBuffer *signalBuffer = m_buffers[ib];

                if (!signalBuffer)
                    continue;

                for (uint32_t ic = 0; ic < signalBuffer->channels; ic++) {
                    auto con = getBufferPortConnection(ib, ic);

                    // NON-interleaved RW of PCM data (c0c0c0... c1c1c1c1... cNcNcNcN)
                    if (con->isOutput) {
                        signalBuffer->getBlock(ic, pcmOutPtr + (ic * blockSize), blockSize);
                    } else {
                        signalBuffer->addBlock(ic, pcmInPtr + (ic * blockSize), blockSize);
                    }
                }
            }


            // write playback data
            while ((outframes = snd_pcm_writei(playback_handle, pcmOutPtr, blockSize)) < 0) {
                if (outframes == -EAGAIN)
                    continue;
                m_numUnderruns++;
                restarting = true;
                snd_pcm_prepare(playback_handle);
            }
            if (outframes != blockSize){
                m_numShortWrites++;
                fprintf(stderr, "Short write to playback device: %d, expecting %d\n", outframes, blockSize);
            }

            processSignalBufferObserverInAudioThread(blockSize);
    }



}


    /*
    AudioDriver::Request &AudioDriver::Request::addStreamer(SignalStreamer *streamer, const std::string &name, Connect connection, int channel) {
        if ((connection & Connect::ToCapture) == Connect::ToCapture) {
            streamer->in = driver->createSignalPort(name + "-in", Connect::ToCapture, channel);
        }

        if (connection != Connect::ToCapture) {
            streamer->out = driver->createSignalPort(name + "-out", ((connection & Connect::ToPlayback) == Connect::ToPlayback) ? Connect::ToPlayback : Connect::ToPlaybackSource, channel);
        }

        actions.push(std::bind(&AudioDriver::addStreamer, driver, streamer));
        return *this;
    }
    */
}
