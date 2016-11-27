#pragma once

#include "audio_driver.h"

#if defined(_WIN32) && !defined(WIN32)
typedef void* jack_native_thread_t;
#endif

#include <jack/jack.h>


namespace autil {
    class AudioDriverJack : public AudioDriverBase
	{
	public:

		typedef std::vector<const char *> PortArray;

		enum class Connect : int {
			ToCapture = 2 << 0,
			ToPlayback = 2<<1,

			ToPlaybackSource = 2 << 2,
		};


        	AudioDriverJack(const std::string &name);
        	~AudioDriverJack();


		void setBlockSize(int blockSize);

		void muteOthers(bool mute = true);

	private:
		std::vector<jack_port_t*> newPorts(std::string baseName, int nChannels, bool outputNotInput);


		// API:
        void *signalPortNew(Connect connection, uint32_t channel, const std::string &name );


		static int jackProcess(jack_nframes_t nframes, void *arg);
		static void jackShutdown(void *arg);

		jack_client_t* initJack();


		jack_client_t *m_jackClient;

        const char **m_portsCapture;
        const char **m_portsPlayback;

		std::vector<PortArray> m_mutedPorts;
	};

	inline constexpr AudioDriverJack::Connect operator&(AudioDriverJack::Connect __x, AudioDriverJack::Connect __y)
	{
		return static_cast<AudioDriverJack::Connect>(static_cast<int>(__x) & static_cast<int>(__y));
	}

	inline constexpr AudioDriverJack::Connect
		operator|(AudioDriverJack::Connect __x, AudioDriverJack::Connect __y)
	{
		return static_cast<AudioDriverJack::Connect>
			(static_cast<int>(__x) | static_cast<int>(__y));
	}

}
