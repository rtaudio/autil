#include "audio_driver_alsa.h"

#include <vector>
#include <fstream>
#include <string>
#include <string.h>
#include <iostream>
#include <exception>
#include <chrono>

#include "signal_buffer.h"

#include "test.h"

namespace autil {

/* we dont use MMAP, see
 * http://stackoverflow.com/questions/14762103/recording-from-alsa-understanding-memory-mapping
 */
 /* 192khz @ 16 => 150sd => 0.78ms*/
 /* 96khz @ 32 => 175sd => 1.82ms*/
	snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
	int rate = 96000;
	int channels = 2;
	int buffer_size = 0;		/* auto */
	int period_size = 0;		/* auto */
	int latency_min = 32; //32;		/* in frames / 2 */
	int latency_max = 2048;		/* in frames / 2 */
	int block = 0;			/* block mode */
	int resample = 1;

	int setparams_stream(snd_pcm_t *handle,
		snd_pcm_hw_params_t *params,
		const char *id)
	{
		int err;
		unsigned int rrate;

		err = snd_pcm_hw_params_any(handle, params);
		if (err < 0) {
			printf("Broken configuration for %s PCM: no configurations available: %s\n", snd_strerror(err), id);
			return err;
		}
		err = snd_pcm_hw_params_set_rate_resample(handle, params, resample);
		if (err < 0) {
			printf("Resample setup failed for %s (val %i): %s\n", id, resample, snd_strerror(err));
			return err;
		}
		err = snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
		if (err < 0) {
			printf("Access type not available for %s: %s\n", id, snd_strerror(err));
			return err;
		}
		err = snd_pcm_hw_params_set_format(handle, params, format);
		if (err < 0) {
			printf("Sample format not available for %s: %s\n", id, snd_strerror(err));
			return err;
		}
		err = snd_pcm_hw_params_set_channels(handle, params, channels);
		if (err < 0) {
			printf("Channels count (%i) not available for %s: %s\n", channels, id, snd_strerror(err));
			return err;
		}
		rrate = rate;
		err = snd_pcm_hw_params_set_rate_near(handle, params, &rrate, 0);
		if (err < 0) {
			printf("Rate %iHz not available for %s: %s\n", rate, id, snd_strerror(err));
			return err;
		}
		if ((int)rrate != rate) {
			printf("Rate doesn't match (requested %iHz, get %iHz)\n", rate, err);
			return -EINVAL;
		}
		std::cout << "Sampling rate:" << rrate << std::endl;
		return 0;
	}

	int setparams_bufsize(snd_pcm_t *handle,
		snd_pcm_hw_params_t *params,
		snd_pcm_hw_params_t *tparams,
		snd_pcm_uframes_t bufsize,
		const char *id)
	{
		int err;
		snd_pcm_uframes_t periodsize;

		snd_pcm_hw_params_copy(params, tparams);
		periodsize = bufsize * 2;
		err = snd_pcm_hw_params_set_buffer_size_near(handle, params, &periodsize);
		if (err < 0) {
			printf("Unable to set buffer size %li for %s: %s\n", bufsize * 2, id, snd_strerror(err));
			return err;
		}
		if (period_size > 0)
			periodsize = period_size;
		else
			periodsize /= 2;
		err = snd_pcm_hw_params_set_period_size_near(handle, params, &periodsize, 0);
		if (err < 0) {
			printf("Unable to set period size %li for %s: %s\n", periodsize, id, snd_strerror(err));
			return err;
		}
		return 0;
	}

	int setparams_set(snd_pcm_t *handle,
		snd_pcm_hw_params_t *params,
		snd_pcm_sw_params_t *swparams,
		const char *id)
	{
		int err;
		snd_pcm_uframes_t val;

		err = snd_pcm_hw_params(handle, params);
		if (err < 0) {
			printf("Unable to set hw params for %s: %s\n", id, snd_strerror(err));
			return err;
		}
		err = snd_pcm_sw_params_current(handle, swparams);
		if (err < 0) {
			printf("Unable to determine current swparams for %s: %s\n", id, snd_strerror(err));
			return err;
		}
		err = snd_pcm_sw_params_set_start_threshold(handle, swparams, 0x7fffffff);
		if (err < 0) {
			printf("Unable to set start threshold mode for %s: %s\n", id, snd_strerror(err));
			return err;
		}
		if (!block)
			val = 4;
		else
			snd_pcm_hw_params_get_period_size(params, &val, NULL);
		err = snd_pcm_sw_params_set_avail_min(handle, swparams, val);
		if (err < 0) {
			printf("Unable to set avail min for %s: %s\n", id, snd_strerror(err));
			return err;
		}
		err = snd_pcm_sw_params(handle, swparams);
		if (err < 0) {
			printf("Unable to set sw params for %s: %s\n", id, snd_strerror(err));
			return err;
		}
		return 0;
	}

	int setparams(snd_pcm_t *phandle, snd_pcm_t *chandle, int *bufsize)
	{
		int err, last_bufsize = *bufsize;
		snd_pcm_hw_params_t *pt_params, *ct_params;	/* templates with rate, format and channels */
		snd_pcm_hw_params_t *p_params, *c_params;
		snd_pcm_sw_params_t *p_swparams, *c_swparams;
		snd_pcm_uframes_t p_size, c_size, p_psize, c_psize;
		unsigned int p_time, c_time;
		unsigned int val;

		snd_pcm_hw_params_alloca(&p_params);
		snd_pcm_hw_params_alloca(&c_params);
		snd_pcm_hw_params_alloca(&pt_params);
		snd_pcm_hw_params_alloca(&ct_params);
		snd_pcm_sw_params_alloca(&p_swparams);
		snd_pcm_sw_params_alloca(&c_swparams);
		if ((err = setparams_stream(phandle, pt_params, "playback")) < 0) {
			printf("Unable to set parameters for playback stream: %s\n", snd_strerror(err));
			exit(0);
		}
		if ((err = setparams_stream(chandle, ct_params, "capture")) < 0) {
			printf("Unable to set parameters for playback stream: %s\n", snd_strerror(err));
			exit(0);
		}

		if (buffer_size > 0) {
			*bufsize = buffer_size;
			goto __set_it;
		}

	__again:
		if (buffer_size > 0)
			return -1;
		if (last_bufsize == *bufsize)
			*bufsize += 4;
		last_bufsize = *bufsize;
		if (*bufsize > latency_max)
			return -1;
	__set_it:
		if ((err = setparams_bufsize(phandle, p_params, pt_params, *bufsize, "playback")) < 0) {
			printf("Unable to set sw parameters for playback stream: %s\n", snd_strerror(err));
			exit(0);
		}
		if ((err = setparams_bufsize(chandle, c_params, ct_params, *bufsize, "capture")) < 0) {
			printf("Unable to set sw parameters for playback stream: %s\n", snd_strerror(err));
			exit(0);
		}

		snd_pcm_hw_params_get_period_size(p_params, &p_psize, NULL);
		if (p_psize > (unsigned int)*bufsize)
			*bufsize = p_psize;
		snd_pcm_hw_params_get_period_size(c_params, &c_psize, NULL);
		if (c_psize > (unsigned int)*bufsize)
			*bufsize = c_psize;
		snd_pcm_hw_params_get_period_time(p_params, &p_time, NULL);
		snd_pcm_hw_params_get_period_time(c_params, &c_time, NULL);
		if (p_time != c_time)
			goto __again;

		snd_pcm_hw_params_get_buffer_size(p_params, &p_size);
		if (p_psize * 2 < p_size) {
			snd_pcm_hw_params_get_periods_min(p_params, &val, NULL);
			if (val > 2) {
				printf("playback device does not support 2 periods per buffer\n");
				exit(0);
			}
			goto __again;
		}
		snd_pcm_hw_params_get_buffer_size(c_params, &c_size);
		if (c_psize * 2 < c_size) {
			snd_pcm_hw_params_get_periods_min(c_params, &val, NULL);
			if (val > 2) {
				printf("capture device does not support 2 periods per buffer\n");
				exit(0);
			}
			goto __again;
		}
		if ((err = setparams_set(phandle, p_params, p_swparams, "playback")) < 0) {
			printf("Unable to set sw parameters for playback stream: %s\n", snd_strerror(err));
			exit(0);
		}
		if ((err = setparams_set(chandle, c_params, c_swparams, "capture")) < 0) {
			printf("Unable to set sw parameters for playback stream: %s\n", snd_strerror(err));
			exit(0);
		}

		if ((err = snd_pcm_prepare(phandle)) < 0) {
			printf("Prepare error: %s\n", snd_strerror(err));
			exit(0);
		}

		//snd_pcm_dump(phandle, output);
		//snd_pcm_dump(chandle, output);
		//fflush(stdout);
		return 0;
	}

void setscheduler(void)
{
        struct sched_param sched_param;
        if (sched_getparam(0, &sched_param) < 0) {
                printf("Scheduler getparam failed...\n");
                return;
        }
        sched_param.sched_priority = sched_get_priority_max(SCHED_RR);
        if (!sched_setscheduler(0, SCHED_RR, &sched_param)) {
                printf("Scheduler set to Round Robin with priority %i...\n", sched_param.sched_priority);
                fflush(stdout);
                return;
        }
        printf("!!!Scheduler set to Round Robin with priority %i FAILED!!!\n", sched_param.sched_priority);
}

long readbuf(snd_pcm_t *handle, char *buf, long len, size_t *frames, size_t *max)
{
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
        } else {
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
long writebuf(snd_pcm_t *handle, char *buf, long len, size_t *frames)
{
        long r;
        while (len > 0) {
                r = snd_pcm_writei(handle, buf, len);
                if (r == -EAGAIN)
                        continue;
                // printf("write = %li\n", r);
                if (r < 0)
                        return r;
                // showstat(handle, 0);
                buf += r * 4;
                len -= r;
                *frames += r;
        }
        return 0;
}


int throwIfError(int err, const std::string &msg) {
    if(err < 0) {
        throw std::runtime_error(msg + " ("+std::string(snd_strerror(err))+")");
    }
}

    AudioDriverAlsa::AudioDriverAlsa(const std::string &deviceName, const StreamProperties &props)
        : AudioDriverBase(deviceName)
	{
		capture_handle = nullptr;
		playback_handle = nullptr;
		
		m_poll = false;

        throwIfError(snd_pcm_open(&playback_handle, deviceName.c_str(), SND_PCM_STREAM_PLAYBACK, block ? 0 : SND_PCM_NONBLOCK),
                     "cannot open output audio device "+deviceName);

         throwIfError(snd_pcm_open(&capture_handle, deviceName.c_str(), SND_PCM_STREAM_CAPTURE, block ? 0 : SND_PCM_NONBLOCK),
                 "cannot open input audio device "+deviceName);

         rate = m_sampleRate = props.sampleRate;
         m_numChannelsCapture = props.numChannelsCapture;
         m_numChannelsPlayback = props.numChannelsPlayback;
        //setBlockSize(props.blockSize);


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
		//throw std::runtime_error("not implemented");
		
		/*
        sync();
        m_actionQueue.push([this]() {
			if (playback_handle)
				configure_alsa_audio(playback_handle, m_numChannelsPlayback, m_blockSize, m_sampleRate);
			if (capture_handle)
				configure_alsa_audio(capture_handle, m_numChannelsCapture, m_blockSize, m_sampleRate);
		});
        commit();
		*/
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

	void gettimestamp(snd_pcm_t *handle, snd_timestamp_t *timestamp)
{
        int err;
        snd_pcm_status_t *status;
        snd_pcm_status_alloca(&status);
        if ((err = snd_pcm_status(handle, status)) < 0) {
                printf("Stream status error: %s\n", snd_strerror(err));
                exit(0);
        }
        snd_pcm_status_get_trigger_tstamp(status, timestamp);
}

    void AudioDriverAlsa::process()
    {
		
		  
		int blockSize = m_blockSize;
		int blockSizeMax = latency_max;
		
		state.reset();

		// create buffers
		std::vector<int16_t> pcmIn, pcmOut;
		
		pcmIn.resize(m_numChannelsCapture * blockSizeMax);
		pcmOut.resize(m_numChannelsPlayback * blockSizeMax);
		auto pcmInPtr = pcmIn.data();
		auto pcmOutPtr = pcmOut.data();		
		auto pcmInBufferPtr = (char*)pcmIn.data();
		auto pcmOutBufferPtr = (char*)pcmOut.data();


		
		int latency = latency_min - 4;
		int err;
		snd_pcm_sframes_t delay;

		state.tStarted = std::chrono::high_resolution_clock::now();

		// XRun-restart loop
		while (m_running) {
			state.restart = false;
			std::chrono::duration<double, std::milli> passedMs = std::chrono::high_resolution_clock::now() - state.tStarted;


			// ignore xruns first 8 seconds to let the CPU exit from power saving state
			if (passedMs.count() < 6000.0) {
				latency = latency_min - 4;
				// reset buffer observers on XRUN
				for (int ip = 0; ip < MAX_SIGNAL_BUFFERS; ip++) {
					auto bufferPool = m_bufferPool[ip];

					if (bufferPool)
						bufferPool->resetBuffers();
				}
			}
			else {
				state.show();
			}
			
                if (setparams(playback_handle, capture_handle, &latency) < 0)
                        break;
				m_sampleRate = rate;

                //showlatency(latency);				
				std::cout << "Block Size:" << latency << std::endl;
				
                throwIfError(snd_pcm_link(capture_handle, playback_handle), "streams link error");	
				
				// 0-set frames
                throwIfError(snd_pcm_format_set_silence(format, pcmOutBufferPtr, latency*channels), "silence error");
				throwIfError(snd_pcm_format_set_silence(format, pcmInBufferPtr, latency*channels), "silence error");               
				
				// fill playback buffer
                throwIfError(writebuf(playback_handle, pcmOutBufferPtr, latency, &state.numFramesOut), "write error");				
                throwIfError(writebuf(playback_handle, pcmOutBufferPtr, latency, &state.numFramesOut), "write error");

                throwIfError(snd_pcm_start(capture_handle), "start error");
				

				gettimestamp(playback_handle, &state.tPlaybackStarted);
				gettimestamp(capture_handle, &state.tCaptureStarted);

					
					ssize_t r;
                size_t in_max = 0;
                while (m_running && !state.restart) {
					processActionQueueInAudioThread();   
					
                        if (m_poll) {
                                snd_pcm_wait(capture_handle, 1000);
                        }
						

						snd_pcm_delay(capture_handle, &delay);
						if (delay > state.maxDelayCapture)
							state.maxDelayCapture = delay;
						
						// read capture samples
                        if ((r = readbuf(capture_handle, pcmInBufferPtr, latency, &state.numFramesIn, &state.maxInLatency)) < 0) {
							// overrun
							state.numOverruns++;
							state.restart = true;
						}
						
						
						            // signal buffers

			// stride (byte-unit)
			size_t stride = (snd_pcm_format_width(format) / 8) * m_numChannelsPlayback;

            for (int ib = 0; ib < MAX_SIGNAL_BUFFERS; ib++) {
                SignalBuffer *signalBuffer = m_buffers[ib];

                if (!signalBuffer)
                    continue;

                for (uint32_t ic = 0; ic < signalBuffer->channels; ic++) {
                    auto con = getBufferPortConnection(ib, ic);

                    // interleaved RW of PCM data (c0c1c2c0c1c3 ...)
                    if (con->isOutput) {
                        signalBuffer->getBlock(ic, reinterpret_cast<uint8_t*>(pcmOutPtr + ic), stride, latency, &float2short);
                    } else {
                        signalBuffer->addBlock(ic, reinterpret_cast<uint8_t*>(pcmInPtr + ic), stride, latency, &short2float);
                    }
                }
            }
						
							// test noise
		//for (int ii = 0; ii < latency; ii++) {
		//		(pcmOutPtr)[0 + (ii*m_numChannelsPlayback)] = (rand() % (32760 * 2)) - 3276;
		//	}
						
			
			snd_pcm_delay(playback_handle, &delay);
			if (delay > state.maxDelayPlayback)
				state.maxDelayPlayback = delay;
			
						
						// write playback samples                        
                        if (writebuf(playback_handle, pcmOutBufferPtr, latency, &state.numFramesOut) < 0) {
							// underrun
							state.numUnderruns++;
							state.restart = true;						
                        }
						
						processSignalBufferObserverInAudioThread(latency);
                }
				

                snd_pcm_drop(capture_handle);
                snd_pcm_nonblock(playback_handle, 0);
                snd_pcm_drain(playback_handle);
                snd_pcm_nonblock(playback_handle, !block ? 1 : 0);
				
                snd_pcm_unlink(capture_handle);
                snd_pcm_hw_free(playback_handle);
                snd_pcm_hw_free(capture_handle);
        }

		state.show();
		
        snd_pcm_close(playback_handle);
        snd_pcm_close(capture_handle);
		playback_handle = nullptr;
		capture_handle = nullptr;
		
        return;

		/*
 
			// Vector processing: http://stackoverflow.com/questions/16031149/speedup-a-short-to-float-cast
			// in JACK look for write_via_copy
			// TODO: vector optimiazation



		*/

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
void AudioDriverAlsa::ProcessState::show() {
	std::cout << "framesIn|Out: " << numFramesIn << " | " << numFramesOut << std::endl;
	std::cout << "maxInLatency:" << maxInLatency << std::endl;
	std::cout << "maxDelay{Playback|Capture}:" << maxDelayPlayback << " | " << maxDelayCapture << std::endl;
	std::cout << "total {over|under}Runs: " << numOverruns << " | " << numUnderruns << std::endl;
	std::cout << "hwSync: " << isHwSync() << std::endl;
}

bool AudioDriverAlsa::ProcessState::isHwSync() {
	return (tPlaybackStarted.tv_sec == tCaptureStarted.tv_sec &&
	    tPlaybackStarted.tv_usec == tCaptureStarted.tv_usec);
}


void AudioDriverAlsa::listDevices() {
	int card = -1;
	if (snd_card_next(&card) < 0 || card < 0) {
		return;
	}

	k
}

}


