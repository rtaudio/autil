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
configure_alsa_audio(snd_pcm_t *device, int channels, int &buffer_size, int sample_rate)
{
    std::cout << "config alsa device C=" << channels << " bs=" << buffer_size << " sr=" << sample_rate << std::endl;
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
                 "cannot set sample format SND_PCM_FORMAT_S16_LE");

	int resample = 1;
	throwIfError(snd_pcm_hw_params_set_rate_resample(device, hw_params, resample),
		"cannot setup resample");

    tmp = sample_rate;
    throwIfError(snd_pcm_hw_params_set_rate_near(device, hw_params, &tmp, 0),
                 "cannot set sample rate");

    if (tmp != sample_rate) {
        fprintf(stderr, "Could not set requested sample rate, asked for %d got %d\n", sample_rate, tmp);
        sample_rate = tmp;
    }

    throwIfError(snd_pcm_hw_params_set_channels(device, hw_params, channels),
            "cannot set channel count");

    throwIfError(snd_pcm_hw_params_set_periods(device, hw_params, fragments, 0),
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

	   throwIfError(snd_pcm_hw_params(device, hw_params), "Error setting HW params");
        
       return 0;
   }


    AudioDriverAlsa::AudioDriverAlsa(const std::string &deviceName, const StreamProperties &props)
        : AudioDriverBase(deviceName)
	{
		const bool block = false;

		capture_handle = nullptr;
		playback_handle = nullptr;

        throwIfError(snd_pcm_open(&playback_handle, deviceName.c_str(), SND_PCM_STREAM_PLAYBACK, block ? 0 : SND_PCM_NONBLOCK),
                     "cannot open output audio device "+deviceName);

         throwIfError(snd_pcm_open(&capture_handle, deviceName.c_str(), SND_PCM_STREAM_CAPTURE, block ? 0 : SND_PCM_NONBLOCK),
                 "cannot open input audio device "+deviceName);

         m_sampleRate = props.sampleRate;
         m_numChannelsCapture = props.numChannelsCapture;
         m_numChannelsPlayback = props.numChannelsPlayback;
        setBlockSize(props.blockSize);


		m_running = true;

        m_audioThread = new RttThread([this]() {
			std::cout << "audioThread started!" << std::endl;
            process();
        }, true, "audio");
	}

    AudioDriverAlsa::~AudioDriverAlsa()
    {
		m_running = false;
		std::cout << "close audio" << std::endl;
        delete m_audioThread;

        if(playback_handle)
            snd_pcm_close(playback_handle);
        if(capture_handle)
            snd_pcm_close(capture_handle);

		playback_handle = nullptr;
		capture_handle = nullptr;
    }

    void AudioDriverAlsa::setBlockSize(int blockSize) {
        m_blockSize = blockSize;

        sync();
        m_actionQueue.push([this]() {
			if (playback_handle)
				configure_alsa_audio(playback_handle, m_numChannelsPlayback, m_blockSize, m_sampleRate);
			if (capture_handle)
				configure_alsa_audio(capture_handle, m_numChannelsCapture, m_blockSize, m_sampleRate);
		});
        commit();
    }


	void float2short(uint8_t *out, size_t stride, const float *in, size_t n)
	{
		stride = stride * sizeof(uint8_t) / sizeof(int16_t);
		for(size_t i = 0; i < n; i++) {
			reinterpret_cast<int16_t*>(out)[i*stride] = lrintf(in[i] * 32767.0f);
		}
	}

	void short2float(float *out, const uint8_t *in, size_t stride, size_t n) {
		const float scaling = 1.0f / 32767.0f;
		stride = stride * sizeof(uint8_t) / sizeof(int16_t);
		for (size_t i = 0; i < n; i++) {
			out[i] = reinterpret_cast<const int16_t*>(in)[i*stride] * scaling;
		}
	}

	/*
	long readbuf(snd_pcm_t *handle, char *buf, long len, size_t *frames, size_t *max)
	{
		const bool block = false;

		long r;

		if (!block) {
			do {
				r = snd_pcm_readi(handle, buf, len);
			} while (r == -EAGAIN);
			if (r > 0) {
				*frames += r;
				if ((long)*max < r)
					*max = r;
			}
			// printf("read = %li\n", r);
		}
		else {
			int frame_bytes = (snd_pcm_format_width(format) / 8) * channels;
			do {
				r = snd_pcm_readi(handle, buf, len);
				if (r > 0) {
					buf += r * frame_bytes;
					len -= r;
					*frames += r;
					if ((long)*max < r)
						*max = r;
				}
				// printf("r = %li, len = %li\n", r, len);
			} while (r >= 1 && len > 0);
		}
		// showstat(handle, 0);
		return r;
	}
	*/

	void writebuf(snd_pcm_t *handle, char *buf, size_t bufStride, long len, size_t *frames)
	{
		long r;

		while (len > 0) {
			r = snd_pcm_writei(handle, buf, len);
			if (r == -EAGAIN)
				continue;
			throwIfError(r, "write error");
			buf += r * bufStride;
			len -= r;
			*frames += r;
		}
	}

    void AudioDriverAlsa::process()
    {
		const unsigned int                 fragments = 2;
		int blockSize = m_blockSize;

        m_numShortWrites = 0;
        m_numUnderruns = 0;
        m_numShortReads = 0;
        m_numOverruns = 0;

		size_t framesOut = 0;
		size_t framesIn = 0;
		size_t maxInLatency = 0;

		const size_t bytesPerSample = 2;

		// create buffers
		std::vector<int16_t> pcmIn, pcmOut;
		pcmIn.resize(m_numChannelsCapture * blockSize);
		pcmOut.resize(m_numChannelsPlayback * blockSize);
		auto pcmInPtr = pcmIn.data();
		auto pcmOutPtr = pcmOut.data();

		
		throwIfError(snd_pcm_prepare(playback_handle), "failed to prepare playback stream");
		throwIfError(snd_pcm_prepare(capture_handle), "failed to prepare capture stream");


		if (capture_handle && playback_handle) {
			throwIfError(snd_pcm_link(capture_handle, playback_handle),
				"streams link error");
		}

		// pre-fill playback buffer
		if (playback_handle) {
			for (int i = 0; i < fragments; i++) {
				writebuf(playback_handle, (char*)pcmOutPtr, bytesPerSample*m_numChannelsPlayback, blockSize, &framesOut);
			}
		}

		throwIfError(snd_pcm_start(capture_handle ? capture_handle : playback_handle),
			"start error");


        int           inframes, outframes, frame_size;
        bool restarting = false;
        

       

        while (m_running) {
            processActionQueueInAudioThread();

            if (restarting) {
                restarting = false;

				/*
				snd_pcm_drop(chandle);
                snd_pcm_nonblock(phandle, 0);
                snd_pcm_drain(phandle);
                snd_pcm_nonblock(phandle, !block ? 1 : 0);
				*/

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

           

            // read capture data
			auto avail = snd_pcm_avail_update(capture_handle);
			if (avail > 0 || true) {
				while ((inframes = snd_pcm_readi(capture_handle, pcmInPtr, blockSize)) < 0) {
					if (inframes == -EAGAIN)
						continue;
					std::cerr << "overrun " << m_numOverruns << std::endl;
					m_numOverruns++;
					restarting = true;
					snd_pcm_prepare(capture_handle);
				}
			

				if (inframes != blockSize) {
					m_numShortReads++;
					fprintf(stderr, "Short read from capture device: %d, expecting %d\n", inframes, blockSize);
					// todo: zero set buffer at [inframes...blockSize]
				}
			}
			/**/

			// Vector processing: http://stackoverflow.com/questions/16031149/speedup-a-short-to-float-cast
			// in JACK look for write_via_copy
			// TODO: vector optimiazation

            // signal buffers

			// stride (byte-unit)
			size_t stride = sizeof(int16_t) * m_numChannelsPlayback;

            for (int ib = 0; ib < MAX_SIGNAL_BUFFERS; ib++) {
                SignalBuffer *signalBuffer = m_buffers[ib];

                if (!signalBuffer)
                    continue;

                for (uint32_t ic = 0; ic < signalBuffer->channels; ic++) {
                    auto con = getBufferPortConnection(ib, ic);

                    // interleaved RW of PCM data (c0c1c2c0c1c3 ...)
                    if (con->isOutput) {
                        signalBuffer->getBlock(ic, reinterpret_cast<uint8_t*>(pcmOutPtr + ic), stride, blockSize, &float2short);
                    } else {
                        signalBuffer->addBlock(ic, reinterpret_cast<uint8_t*>(pcmInPtr + ic), stride, blockSize, &short2float);
                    }
                }
            }

			//for (int ii = 0; ii < blockSize; ii++) {
				//(pcmOutPtr)[0 + (ii*m_numChannelsPlayback)] = (rand() % (32760 * 2)) - 3276;
			//}


            // write playback data
            while ((outframes = snd_pcm_writei(playback_handle, pcmOutPtr, blockSize)) < 0) {
                if (outframes == -EAGAIN)
                    continue;
				if(m_numUnderruns % 100000 == 0)
					std::cerr << "underrun " << m_numUnderruns << std::endl;
                m_numUnderruns++;
                //restarting = true;
                //snd_pcm_prepare(playback_handle);

				//for (int i = 0; i < fragments; i += 1)
				//	snd_pcm_writei(playback_handle, pcmOut.data(), blockSize);
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
