#include <sndfile.hh>
#include <limits>

#include "file_io.h"

namespace autil {
void fileio::readWave(const char * fname, std::vector<float> *buffer)
{
	SndfileHandle file;

	file = SndfileHandle(fname);

	//printf("Opened file '%s'\n", fname);
	//printf("    Sample rate : %d\n", file.samplerate());
	//printf("    Channels    : %d\n", file.channels());

	buffer->resize(file.frames());
	file.read(buffer->data(), buffer->size());
}
namespace fileio {
void
writeWave(const char * fname, std::vector<float> samples, float gain)
{
	SndfileHandle file;
	int channels = 1;
	int srate = 44100;

	file = SndfileHandle(fname, SFM_WRITE, SF_FORMAT_WAV| SF_FORMAT_PCM_16, channels, srate);

	if (gain != 1.0f) {
		for (int i = 0; i < samples.size(); i++) {
			samples[i] *= gain;
		}
	}

	file.write(samples.data(), samples.size());

}

}
}