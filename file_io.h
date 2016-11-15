#pragma once

#include <vector>

namespace autil {
namespace fileio {
	void	readWave(const char * fname, std::vector<float> *buffer);
	void	writeWave(const char * fname, std::vector<float> samples, float gain = 1.0);
}
}

