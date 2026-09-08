#ifndef IQ_CORRECT_HH
#define IQ_CORRECT_HH

#include <complex>

#include "loop_filters.hh"

typedef std::complex<float> complex;

template <typename T>
class iq_correct
{
public:
    iq_correct(int _bit, float _level_max, float _level_min)
    {
        scale = 1.0f / (1 << _bit);
        level_max = _level_max;
        level_min = _level_min;
    }
    ~iq_correct(){}

    void execute(const size_t &_len, const T* _iq_in, complex* _out, int &_gain_offset)
    {
        theta1 = 0.0f;
        theta2 = 0.0f;
        theta3 = 0.0f;
        for(int i = 0; i < _len; ++i){
            complex in = complex(_iq_in[0] * scale, _iq_in[1] * scale);
            _iq_in += 2;
            _out[i] = correct(in);
        }
        _gain_offset = estimations(_len);
    }
    void execute(int _len, T* _i_in, T* _q_in, complex* _out, int &_gain_offset)
    {
        theta1 = 0.0f;
        theta2 = 0.0f;
        theta3 = 0.0f;
        for(int i = 0; i < _len; ++i){
            complex in = complex(_i_in[i] * scale, _q_in[i] * scale);
            _out[i] = correct(in);
        }
        _gain_offset = estimations(_len);
    }

    float level_min;

private:
    int len;
    float scale;
    float level_max;
    float c1 = 0.0f;
    float c2 = 1.0f;
    float theta1, theta2, theta3;
    static constexpr float dc_ratio = 1.0e-5f;//1.0e-5f
    exponential_averager<complex, float, dc_ratio> exp_avg_dc;
    inline complex correct(complex &_in)
    {
        //___DC offset remove____________
        _in -= exp_avg_dc(_in);
        //___IQ imbalance remove_________
        est_1_bit_quantization(_in.real(), _in.imag());
        float real = _in.real() * c2;
        return complex(real, _in.imag() + c1 * real);
        //_____________________________
    }
    inline void est_1_bit_quantization(float _real, float _imag)
    {
        float sgn;
        sgn = _real < 0 ? -1.0f : 1.0f;
        theta1 -= _imag * sgn;
        theta2 += _real * sgn;
        sgn = _imag < 0 ? -1.0f : 1.0f;
        theta3 += _imag * sgn;
    }
    inline int estimations(int _len)
    {
        //___IQ imbalance estimations___
        if(_len<=0 || theta2<=1.0e-12f || theta3<=1.0e-12f) { c1=0; c2=1; return 1; }
        c1 = theta1 / theta2;
        float c_temp = theta3 / theta2;
        float variance=c_temp*c_temp-c1*c1;
        if(!std::isfinite(variance) || variance<=0) { c1=0; c2=1; return 0; }
        c2 = sqrtf(variance);
        //___level gain estimation___
        float level_detect = (theta2 * theta3) / _len / _len;
        if(level_detect < level_min) {

            return 1;

        }
        else if(level_detect > level_max) {

            return -1;

        }
        else{

            return 0;

        }
    }

};

#endif // IQ_CORRECT_HH
