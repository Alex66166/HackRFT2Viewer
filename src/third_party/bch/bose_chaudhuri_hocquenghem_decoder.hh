/* -*- c++ -*- */
/*
 * Copyright 2018 Ahmet Inan, Ron Economos.
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifndef BOSE_CHAUDHURI_HOCQUENGHEM_DECODER_HH
#define BOSE_CHAUDHURI_HOCQUENGHEM_DECODER_HH

#include "bitman.hh"
#include "reed_solomon_error_correction.hh"

namespace CODE {

  template <int ROOTS, int FCR, int MSG, typename GF>
  class BoseChaudhuriHocquenghemDecoder
  {
public:
    typedef typename GF::value_type value_type;
    typedef typename GF::ValueType ValueType;
    typedef typename GF::IndexType IndexType;
    static const int NR = ROOTS;
    static const int N = GF::N, K = MSG, NP = N - K;

private:
    ReedSolomonErrorCorrection<NR, FCR, GF> algorithm;
    // Eight Horner steps per byte. Tables are immutable and shared by all
    // decoders of this code, not rebuilt per frame. Binary BCH also has
    // S(2*i) = S(i)^2, so only odd roots need the byte scan (FCR == 1).
    struct SyndromeTables {
      value_type low[NR][256]{}, high[NR][256]{}, byte[NR][256]{};
      SyndromeTables() {
        for (int i = 0; i < NR; ++i) {
          if (FCR == 1 && ((i + 1) % 2 == 0)) continue;
          IndexType step(FCR + i), step8(8 * (FCR + i));
          for (int b = 0; b < 256; ++b) {
            low[i][b] = value_type(int(ValueType(b) * step8));
            if (N > 255 && (b << 8) <= N)
              high[i][b] = value_type(int(ValueType(b << 8) * step8));
            ValueType v(0);
            for (int bit = 7; bit >= 0; --bit)
              v = fma(step, v, ValueType((b >> bit) & 1));
            byte[i][b] = value_type(int(v));
          }
        }
      }
    };
    static const SyndromeTables &syndrome_tables() {
      static const SyndromeTables tables;
      return tables;
    }
    void update_syndromes(uint8_t *poly, ValueType *syndromes, int begin, int end)
    {
      const auto &tables = syndrome_tables();
      for (int i = 0; i < NR; ++i) {
        if (FCR == 1 && ((i + 1) % 2 == 0)) continue;
        int j = begin;
        ValueType v = syndromes[i];
        IndexType root(FCR + i);
        while (j < end && (j & 7))
          v = fma(root, v, ValueType(get_be_bit(poly, j++)));
        for (; j + 8 <= end; j += 8) {
          const unsigned previous = int(v);
          v = ValueType(tables.low[i][previous & 255] ^
                        tables.high[i][previous >> 8] ^ tables.byte[i][poly[j >> 3]]);
        }
        while (j < end)
          v = fma(root, v, ValueType(get_be_bit(poly, j++)));
        syndromes[i] = v;
      }
    }

public:
    int
    compute_syndromes(uint8_t *data,
                      uint8_t *parity,
                      ValueType *syndromes,
                      int data_len = K)
    {
      assert(0 < data_len && data_len <= K);
      // $syndromes_i = code(pe^{FCR+i})$
      ValueType coeff(get_be_bit(data, 0));
      for (int i = 0; i < NR; ++i) {
        syndromes[i] = coeff;
      }
      update_syndromes(data, syndromes, 1, data_len);
      update_syndromes(parity, syndromes, 0, NP);
      if (FCR == 1) {
        for (int root = 2; root <= NR; root += 2) {
          ValueType half = syndromes[root / 2 - 1];
          syndromes[root - 1] = half * half;
        }
      }
      int nonzero = 0;
      for (int i = 0; i < NR; ++i) {
        nonzero += !!syndromes[i];
      }
      return nonzero;
    }

    int
    compute_syndromes(uint8_t *data,
                      uint8_t *parity,
                      value_type *syndromes,
                      int data_len = K)
    {
      return compute_syndromes(
          data, parity, reinterpret_cast<ValueType *>(syndromes), data_len);
    }

    int
    operator()(uint8_t *data,
               uint8_t *parity,
               value_type *erasures = 0,
               int erasures_count = 0,
               int data_len = K)
    {
      assert(0 <= erasures_count && erasures_count <= NR);
      assert(0 < data_len && data_len <= K);
      if (0) {
        for (int i = 0; i < erasures_count; ++i) {
          int idx = (int) erasures[i];
          if (idx < data_len) {
            set_be_bit(data, idx, 0);
          }
          else {
            set_be_bit(parity, idx - data_len, 0);
          }
        }
      }
      if (erasures_count && data_len < K) {
        for (int i = 0; i < erasures_count; ++i) {
          erasures[i] += K - data_len;
        }
      }
      ValueType syndromes[NR];
      if (!compute_syndromes(data, parity, syndromes, data_len)) {
        return 0;
      }
      IndexType locations[NR];
      ValueType magnitudes[NR];
      int count = algorithm(syndromes,
                            locations,
                            magnitudes,
                            reinterpret_cast<IndexType *>(erasures),
                            erasures_count);
      if (count <= 0) {
        return count;
      }
      for (int i = 0; i < count; ++i) {
        if ((int) locations[i] < K - data_len) {
          return -1;
        }
      }
      for (int i = 0; i < count; ++i) {
        if (1 < (int) magnitudes[i]) {
          return -1;
        }
      }
      for (int i = 0; i < count; ++i) {
        int idx = (int) locations[i] + data_len - K;
        bool err = (bool) magnitudes[i];
        if (idx < data_len) {
          xor_be_bit(data, idx, err);
        }
        else {
          xor_be_bit(parity, idx - data_len, err);
        }
      }
      // Never accept a miscorrection as a valid BBFRAME.
      if (compute_syndromes(data, parity, syndromes, data_len)) return -1;
      int corrections_count = 0;
      for (int i = 0; i < count; ++i) {
        corrections_count += !!magnitudes[i];
      }
      return corrections_count;
    }
  };
} // namespace CODE

#endif
