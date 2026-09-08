/*
 *  Copyright 2020 Oleg Malyutin.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#ifndef INTERPOLATOR_FARROW_HH
#define INTERPOLATOR_FARROW_HH

template<typename T, typename F>
class interpolator_farrow_4pt_3rd // 4-point, 3rd-order Hermite (z-form)
{
private:
    const F k_1_2       = 1.0 / 2.0;
    const F k_1_4       = 1.0 / 4.0;
    const F k_1_8       = 1.0 / 8.0;
    const F k_1_16      = 1.0 / 16.0;
    const F k_3_2       = 3.0 / 2.0;
    const F k_9_16      = 9.0 / 16.0;
    const F k_11_8      = 11.0 / 8.0;
    T delay_data_1 = 0;
    T delay_data_2 = 0;
    T delay_data_3 = 0;
    const F start = -0.5;
    const F tr = 1.0 + start;
    const F next = 1.0;
    F x1 = start;

public:
    interpolator_farrow_4pt_3rd() {}
    ~interpolator_farrow_4pt_3rd() {}
    void reset(){delay_data_1=delay_data_2=delay_data_3=T{};x1=start;}

    void operator()(int len_in_, T* in_, double &arbitrary_resample_, int &len_out_, T* out_)
    {
        const int len_in = len_in_;
        int idx_out = 0;
        const F delay_x = arbitrary_resample_;
        for(int i = 0; i < len_in; ++i) {
            T in = in_[i];
            T even1 = delay_data_3 + in;
            T even2 = delay_data_2 + delay_data_1;
            T odd1 = delay_data_3 - in;
            T odd2 = delay_data_2 - delay_data_1;
            T a0 = k_9_16 * even2 - k_1_16 * even1;
            T a1 = k_1_8 * odd1 - k_11_8 * odd2;
            T a2 = k_1_4 * (even1 - even2);
            T a3 = k_3_2 * odd2 - k_1_2 * odd1;
            while(x1 < tr){
                F x2 = x1 * x1;
                F x3 = x2 * x1;
                out_[idx_out++] =  a3 * x3 + a2 * x2 + a1 * x1 + a0;
                x1 += delay_x;
            }
            x1 -= next;
            delay_data_3 = delay_data_2;
            delay_data_2 = delay_data_1;
            delay_data_1 = in;
        }
        len_out_ = idx_out;
    }

};

template<typename T, typename F>
class interpolator_farrow_6pt_5rd // 6-point, 5rd-order Hermite (x-form)
{
private:
    const F k_1_12   = 1.0 / 12.0;
    const F k_11_24  = 11.0 / 24.0;
    const F k_1_8    = 1.0 / 8.0;
    const F k_13_12  = 13.0 / 12.0;
    const F k_2_3    = 2.0 / 3.0;
    const F k_25_12  = 25.0 / 12.0;
    const F k_3_2    = 3.0 / 2.0;
    const F k_5_12   = 5.0 / 12.0;
    const F k_7_12   = 7.0 / 12.0;
    const F k_7_24   = 7.0 / 24.0;
    const F k_1_24   = 1.0 / 24.0;
    const F k_5_24   = 5.0 / 24.0;
    T in[6] = {};
    const F start = -0.5;
    const F tr = 1.0 + start;
    const F next = 1.0;
    F x1 = start;

public:
    interpolator_farrow_6pt_5rd() {}
    ~interpolator_farrow_6pt_5rd() {}

    void operator()(int len_in_, T* in_, double &arbitrary_resample_, int &len_out_, T* out_)
    {
        const int len_in = len_in_;
        int idx_out = 0;
        const F delay_x = arbitrary_resample_;
        for(int i = 0; i < len_in; ++i) {
            in[5] = in_[i];
            T eighthym2 = k_1_8 * in[0];
            T eleventwentyfourthy2 = k_11_24 * in[4];
            T twelfthy3 = k_1_12 * in[5];
            T c0 = in[2];
            T c1 = k_1_12 * (in[0] - in[4]) + k_2_3 * (in[3] - in[1]);
            T c2 = k_13_12 * in[1] - k_25_12 * in[2] + k_3_2 * in[3] - eleventwentyfourthy2 + twelfthy3 - eighthym2;
            T c3 = k_5_12 * in[2] - k_7_12 * in[3] + k_7_24 * in[4] - k_1_24 * (in[0] + in[1] + in[5]);
            T c4 = eighthym2 - k_7_12 * in[1] + k_13_12 * in[2] - in[3] + eleventwentyfourthy2 - twelfthy3;
            T c5 = k_1_24 * (in[5] - in[0]) + k_5_24 * (in[1] - in[4]) + k_5_12 * (in[3] - in[2]);
            while(x1 < tr){
                F x2 = x1 * x1;
                F x3 = x2 * x1;
                F x4 = x3 * x1;
                F x5 = x4 * x1;
                out_[idx_out++] =  c5 * x5 + c4 * x4 + c3 * x3 + c2 * x2 + c1 * x1 + c0;
                x1 += delay_x;
            }
            x1 -= next;
            for(int i = 0; i < 5; ++i){
                in[i] = in[i + 1];
            }
        }
        len_out_ = idx_out;
    }

};

#endif // INTERPOLATOR_FARROW_HH
