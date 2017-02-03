#pragma once

#include <chrono>
#include "audio_driver_base.h"

#include <alsa/asoundlib.h>

/* docs/infos/links:
http://www.alsa-project.org/alsa-doc/alsa-lib/pcm.html
http://git.alsa-project.org/?p=alsa-lib.git;a=tree;f=test
http://www.saunalahti.fi/~s7l/blog/2005/08/21/Full%20Duplex%20ALSA
https://github.com/bear24rw/alsa-utils/tree/master/alsaloop
*/


namespace autil {
    class AudioDriverAlsa : public AudioDriverBase
	{
	public:
        struct StreamProperties {
            int numChannelsCapture;
            int numChannelsPlayback;
            int sampleRate;
            int blockSize;

            StreamProperties() {
                blockSize = 256;
                numChannelsCapture = 2;
                numChannelsPlayback = 2;
                sampleRate = 48000;
            }
        };
            AudioDriverAlsa(const std::string &deviceName, const StreamProperties &props);
        	~AudioDriverAlsa();


		void setBlockSize(int blockSize);
    protected:
        void addSignal(SignalBuffer *buffer, std::vector<void*> ports);

	private:
        RttThread *m_audioThread;
		
		struct ProcessState {
			size_t numFramesIn;
			size_t numFramesOut;
			
			size_t maxInLatency;
			size_t maxDelayPlayback, maxDelayCapture;
			
			bool restart;

			std::chrono::high_resolution_clock::time_point tStarted;
			
			int numOverruns;//number of times the capture buffer was not read in time
			int numUnderruns;//number of times the playback buffers was not filled in time
			
			//int m_numShortWrites;
			//int m_numShortReads;
			
			snd_timestamp_t tPlaybackStarted, tCaptureStarted;
			
			inline void reset() { memset(this, 0, sizeof(*this)); }
			inline ProcessState() { reset(); }

			void show();
			bool isHwSync();
			inline int xruns() { return numOverruns + numUnderruns; }
		};


        ProcessState state;
		
		bool m_poll;//energy saving

		

		snd_pcm_t      *playback_handle;
		snd_pcm_t      *capture_handle;
		
		virtual void process();
	};

}
