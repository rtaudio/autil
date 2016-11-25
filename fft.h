#pragma once
#define NOMINMAX
#undef min
#undef max

#include<unordered_map>
#include<vector>

#include <fftw3.h>
#include <rtt/rtt.h>

namespace autil {

	struct complex_float { float re, im; };

	std::vector<complex_float> operator*(const std::vector<complex_float>& v1, const std::vector<complex_float>& v2);
	std::vector<complex_float> operator/(const std::vector<complex_float>& v1, const std::vector<complex_float>& v2);

	std::vector<float> abs(const std::vector<complex_float> &vector);
	float max(const std::vector<float> &vector);
	float min(const std::vector<float> &vector);

	class fft
	{
	public:
		struct plan {
			fftwf_plan _plan;
			int _size;

			
		};

		struct plan_fft : public plan {
			inline plan_fft(const plan &p) {
				_plan = p._plan;
				_size = p._size;
			};

			inline void execute(std::vector<float> &in, std::vector<float> *out) {
				fftwf_execute_dft_r2c(_plan, in.data(), reinterpret_cast<fftwf_complex*>(out->data()));
			}
			inline void execute(float *in, float *out) {
				fftwf_execute_dft_r2c(_plan, in, reinterpret_cast<fftwf_complex*>(out));
			}
			inline void execute(std::vector<float> &in, std::vector<complex_float> *out) {
				fftwf_execute_dft_r2c(_plan, in.data(), reinterpret_cast<fftwf_complex*>(out->data()));
			}
			inline void execute(float *in, complex_float *out) {
				fftwf_execute_dft_r2c(_plan, in, reinterpret_cast<fftwf_complex*>(out));
			}
			inline void operator()(float *in, std::vector<complex_float> *out) {
				fftwf_execute_dft_r2c(_plan, in, reinterpret_cast<fftwf_complex*>(out->data()));
			}

		};

		struct plan_ifft : public plan {
			inline plan_ifft(const plan &p) {
				_plan = p._plan;
				_size = - std::abs(p._size);
			};

			inline void execute(std::vector<float> &in, std::vector<float> *out) {
				fftwf_execute_dft_c2r(_plan, reinterpret_cast<fftwf_complex*>(in.data()), out->data());
			}
			inline void execute(float *in, float *out) {
				fftwf_execute_dft_c2r(_plan, reinterpret_cast<fftwf_complex*>(in), out);
			}
			inline void execute(std::vector<complex_float> &in, std::vector<float> *out) {
				fftwf_execute_dft_c2r(_plan, reinterpret_cast<fftwf_complex*>(in.data()), out->data());
			}
			inline void execute(complex_float *in, float *out) {
				fftwf_execute_dft_c2r(_plan, reinterpret_cast<fftwf_complex*>(in), out);
			}

			inline std::vector<float> operator()(const std::vector<complex_float> &in) {
				std::vector<float> out(-_size);
				fftwf_execute_dft_c2r(_plan, const_cast<fftwf_complex*>(reinterpret_cast<const fftwf_complex*>(in.data())), out.data());
				return out;
			}
		};

		static inline plan_fft getFFT(int size) {
			return getPlan(size, false);
		}

		static plan_ifft getIFFT(int size) {
			return getPlan(size, true);
		}
	private:
		static plan getPlan(int size, bool ifft, float *in, float *out);
		static plan getPlan(int size, bool ifft = false);

		static std::unordered_map<int, plan> plans;

		static RttMutex planMutex;

	};
}
