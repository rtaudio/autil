#pragma once

#include <vector>

namespace autil {
namespace fileio {
	void	readWave(const std::string &fname, std::vector<float> *buffer);
	void	writeWave(const std::string &fname, std::vector<float> samples, float gain = 1.0);
}
}

