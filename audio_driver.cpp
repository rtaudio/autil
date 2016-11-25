#include "audio_driver.h"

#include <vector>
#include <fstream>
#include <string>
#include <string.h>
#include <iostream>

#include "signal_buffer.h"

#include "test.h"

namespace autil {
	AudioDriver::AudioDriver(const std::string &name) :
		m_totalFramesProcessed(0),
		m_running(false),
		m_newActions(false),
		m_paused(false),
		m_name(name)
	{
		memset(m_buffers, 0, sizeof(m_buffers));
		memset(m_bufferPool, 0, sizeof(m_bufferPool));

		memset(m_bufferPortConnections, 0, sizeof(m_bufferPortConnections));
		
		m_jackClient = initJack();
		m_sampleRate = jack_get_sample_rate(m_jackClient);
		m_blockSize = jack_get_buffer_size(m_jackClient);

		m_running = true;

		m_mutedPorts = std::vector<PortArray>(m_portsPlaybacleNum, std::vector<const char *>());
	}


	AudioDriver::~AudioDriver()
	{
		m_running = false;
		//jack_deactivate(m_jackClient);
		jack_client_close(m_jackClient);
	}

	void AudioDriver::pauseAudioProcessing()
	{
		m_paused = true;
		m_newActions = true;
		m_evtActionQueueProcessed.Wait();
	}



	void AudioDriver::muteOthers(bool mute)
	{
		RttLocalLock ll(m_mtxActionQueue);

		for (int ci = 0; ci < m_portsPlaybacleNum; ci++) {
			if (mute) {
				const char **portsAudioSource = jack_port_get_all_connections(m_jackClient, jack_port_by_name(m_jackClient, m_portsPlayback[ci]));
				if (!portsAudioSource || !portsAudioSource[0]) {
					continue;
				}

				while (auto p = *(portsAudioSource++)) {
					if (jack_disconnect(m_jackClient, p, m_portsPlayback[ci])) {
						throw std::runtime_error("Cannot disconnect playback source from playback port!");
					}

					m_mutedPorts[ci].push_back(p);
				}
			}
			else {
				for (auto p : m_mutedPorts[ci]) {
					if (jack_connect(m_jackClient, p, m_portsPlayback[ci])) {
						throw std::runtime_error("Cannot re-connect playback source to playback port!");
					}
				}
			}
		}

		commit(); // sync with driver
	}

	int uniquePtrArrayAdd(void **array, int len, void* ptr)
	{
		int iEmpty = 0;
		for (int i = (len - 1); i >= 0; i--) {
			if (array[i] == ptr)
				return -1;
			if (array[i] == NULL)
				iEmpty = i;
		}

		array[iEmpty] = ptr;
		return iEmpty;
	}

	int uniquePtrArrayIndexOf(void **array, int len, void* ptr)
	{
		for (int i = (len - 1); i >= 0; i--) {
			if (array[i] == ptr)
				return i;
		}

		return -1;
	}



	void AudioDriver::addObserver(SignalBufferObserver *pool)
	{
		uniquePtrArrayAdd((void**)m_bufferPool, MAX_SIGNAL_BUFFERS, pool);
		pool->lastUpdate = -1;
	}


	std::vector<jack_port_t*> AudioDriver::createSignalPorts(SignalBuffer *buffer, const std::string &name, Connect connection)
	{
		bool isOutput = connection == Connect::ToPlayback;

		std::vector<jack_port_t*> ports;
		buffer->name = name;

		for (uint32_t c = 0; c < buffer->channels; c++) {
			ports.push_back(createSignalPort(name, connection, c));
		}

		return ports;
	}


	jack_port_t* AudioDriver::createSignalPort(std::string name, Connect connection, uint32_t channel)
	{
		bool isOutput = connection == Connect::ToPlayback;

		std::string portName = name + std::to_string(channel);
		auto port = jack_port_register(m_jackClient, portName.c_str(), JACK_DEFAULT_AUDIO_TYPE, isOutput ? JackPortIsOutput : JackPortIsInput, 0);
		if (port == NULL) {
			throw std::runtime_error("Could not create port " + portName);
		}


		switch (connection) {
		case Connect::ToCapture:
			if (m_portsCapture[channel] != NULL) {
				if (jack_connect(m_jackClient, m_portsCapture[channel], jack_port_name(port))) {
					throw std::runtime_error("Cannot connect input port " + portName + " to capture");
				}
			}
			break;

		case Connect::ToPlaybackSource:

			// find ports connected to physical playback ports and "hook in"
			if (m_portsPlayback[channel] != NULL) {
				const char **portsAudioSource = jack_port_get_all_connections(m_jackClient, jack_port_by_name(m_jackClient, m_portsPlayback[channel]));
				if (!portsAudioSource || !portsAudioSource[0]) {
					throw std::runtime_error("Playplack port " + std::string(m_portsPlayback[channel]) + " does not have any source.");
				}

				if (jack_connect(m_jackClient, portsAudioSource[0], jack_port_name(port))) {
					throw std::runtime_error("Cannot connect input port to playback source!");
				}
			}

			break;

		case Connect::ToPlayback:
			if (m_portsPlayback[channel] != NULL) {
				if (jack_connect(m_jackClient, jack_port_name(port), m_portsPlayback[channel])) {
					throw std::runtime_error("Cannot connect to playback port!");
				}
			}
			break;
		default: throw std::invalid_argument("Invalid port connection type.");
		}


		return port;
	}

	void AudioDriver::addSignal(SignalBuffer *buffer, std::vector<jack_port_t*> ports)
	{
		int ib = uniquePtrArrayAdd((void**)m_buffers, MAX_SIGNAL_BUFFERS, buffer);

		if (ib == -1)
			throw std::runtime_error("Signal buffer already added!");

		bool isPlayback = (jack_port_flags(ports[0]) & JackPortIsOutput) == JackPortIsOutput;

		for (uint32_t c = 0; c < buffer->channels; c++) {
			auto con = getBufferPortConnection(ib, c);
			con->isOutput = isPlayback;
			con->port = ports[c];
		}



		buffer->resetIterator();
	}


	void AudioDriver::addStreamer(SignalStreamer *streamer)
	{
		m_streamers.push_back(streamer);
	}


	void AudioDriver::removeSignal(SignalBuffer *buffer)
	{
		int ib = uniquePtrArrayIndexOf((void**)m_buffers, MAX_SIGNAL_BUFFERS, buffer);
		if (ib < 0)
			throw std::runtime_error("Tried to remove unknown SignalBuffer");

		for (uint32_t c = 0; c < buffer->channels; c++) {
			auto con = getBufferPortConnection(ib, c);
			if (!con->port)
				throw std::runtime_error("remove: Port = NULL!");

			jack_port_unregister(m_jackClient, con->port);

			con->port = nullptr;
			con->isOutput = false;
		}

		m_buffers[ib] = nullptr;

	}


	void AudioDriver::removeObserver(SignalBufferObserver *observer)
	{
		int io = uniquePtrArrayIndexOf((void**)m_bufferPool, MAX_SIGNAL_BUFFERS, observer);
		if (io < 0)
			throw std::runtime_error("Tried to remove unknown SignalBufferObserver");
		m_bufferPool[io] = nullptr;
	}


	jack_client_t * AudioDriver::initJack()
	{
		int portNum;
		jack_status_t status;


		jack_client_t *client = jack_client_open(m_name.c_str(), JackNullOption, &status, nullptr);
		if (client == nullptr) {
			throw  std::runtime_error("jack_client_open() failed");
		}

		jack_set_process_callback(client, &AudioDriver::jackProcess, this);
		jack_on_shutdown(client, &AudioDriver::jackShutdown, this);

		m_portsCapture = jack_get_ports(client, nullptr, nullptr, JackPortIsOutput | JackPortIsPhysical);
		if (m_portsCapture == nullptr) {
			throw std::runtime_error("No physical capture ports");
		}

		portNum = 0;
		while (m_portsCapture[++portNum]) {}
		m_portsCaptureNum = portNum;

		m_portsPlayback = jack_get_ports(client, nullptr, nullptr, JackPortIsInput | JackPortIsPhysical);
		if (m_portsPlayback == nullptr) {
			throw std::runtime_error("No physical playback ports");
		}

		portNum = 0;
		while (m_portsPlayback[++portNum]) {}
		m_portsPlaybacleNum = portNum;


		if (jack_activate(client)) {
			throw std::runtime_error("Could not activate client");
		}


		return client;
	}


	int AudioDriver::jackProcess(jack_nframes_t nframes, void *arg)
	{
		AudioDriver *ad = (AudioDriver*)arg;

		if (!ad->m_running)
			return 1;

		if (ad->m_newActions) {
			//printf("AudioDriver: processing queue...\n");

			while (ad->m_actionQueue.size()) {
				try {
					//std::cout << "proc..." << std::endl;
					ad->m_actionQueue.front()();
					//std::cout << "done!" << std::endl;
				}
				catch (const std::exception &ex) {
					std::cout << "AudioDriver exception:" << ex.what() << std::endl;
				}
				ad->m_actionQueue.pop();
			}


			// remove cleared streamers
			ad->m_streamers.erase(std::remove_if(ad->m_streamers.begin(), ad->m_streamers.end(),
				[](SignalStreamer *s) { return !!s->func || (s->in == s->out); }), ad->m_streamers.end());

			ad->m_newActions = false;
			ad->m_evtActionQueueProcessed.Signal();
		}

		if (ad->m_paused)
			return 0;


		// signal buffers
		for (int ib = 0; ib < MAX_SIGNAL_BUFFERS; ib++) {
			SignalBuffer *signalBuffer = ad->m_buffers[ib];

			if (!signalBuffer)
				continue;

			for (uint32_t ic = 0; ic < signalBuffer->channels; ic++) {
				auto con = ad->getBufferPortConnection(ib, ic);

				if (!con->port)
					continue;

				jack_default_audio_sample_t * block = (jack_default_audio_sample_t *)jack_port_get_buffer(con->port, nframes);

				

				if (block == NULL) {
					continue;
					//throw "Block is NULL!";
				}

				if (con->isOutput) {
					signalBuffer->getBlock(ic, block, nframes);
				}
				else {
					signalBuffer->addBlock(ic, block, nframes);
				}				
			}
		}

		// signal buffer observers
		for (int ip = 0; ip < MAX_SIGNAL_BUFFERS; ip++) {
			auto bufferPool = ad->m_bufferPool[ip];

			if (!bufferPool)
				continue;

			if (bufferPool->lastUpdate == -1)
				bufferPool->lastUpdate = ad->m_totalFramesProcessed;

			if (bufferPool->updateInterval >= 0 && (ad->m_totalFramesProcessed - bufferPool->lastUpdate) > bufferPool->updateInterval) {
				bufferPool->lastUpdate = ad->m_totalFramesProcessed;
				if (!bufferPool->commit()) {
					printf("History comit failed! Update thread is too slow.\n");
					bufferPool->lastUpdate += bufferPool->updateInterval; // add penalty time
				}
			}
		}


		// stream processors
		for (auto streamerPtr : ad->m_streamers) {
			auto &streamer(*streamerPtr);

			if (!streamer.func)
				continue;			
			float * blockIn = (streamer.in) ? (float *)jack_port_get_buffer(streamer.in, nframes) : nullptr;
			float * blockOut= (streamer.out) ? (float *)jack_port_get_buffer(streamer.out, nframes) : nullptr;

			if (!streamer.func(blockIn, blockOut, nframes)) {
				streamer.func = nullptr;
				streamer._end();
				ad->m_newActions = true;
			}
		}


		ad->m_totalFramesProcessed += nframes;

		return 0;
	}

	void AudioDriver::jackShutdown(void *arg)
	{
		printf("Jack shutdown\n");
		auto ad = (AudioDriver*)arg;
		ad->m_running = false;
	}

	std::string getError(int rc) {
		char errmsg[1024];
#ifdef _WIN32
		::strerror_s(errmsg, rc);
#else
		strerror_r(rc, errmsg, sizeof errmsg);
#endif
		return errmsg;
	}

	void AudioDriver::setBlockSize(int blockSize)
	{
		auto rc = jack_set_buffer_size(m_jackClient, blockSize);
		if (rc)
			throw std::runtime_error("jack_set_buffer_size(): " + getError(rc));
		m_blockSize = jack_get_buffer_size(m_jackClient);
	}


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

}