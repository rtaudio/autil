#pragma once

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

        int m_numShortWrites;
        int m_numUnderruns;
        int m_numShortReads;
        int m_numOverruns;

		snd_pcm_t      *playback_handle;
		snd_pcm_t      *capture_handle;
		
        void process();
	};

}
