/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2015 Software Radio Systems Limited
 *
 * \section LICENSE
 *
 * This file is part of the srsLTE library.
 *
 * srsLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * srsLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * A copy of the GNU Affero General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

/**********************************************************************************************
 *  File:         chest_dl.h
 *
 *  Description:  3GPP LTE Downlink channel estimator and equalizer.
 *                Estimates the channel in the resource elements transmitting references and
 *                interpolates for the rest of the resource grid.
 *                The equalizer uses the channel estimates to produce an estimation of the
 *                transmitted symbol.
 *                This object depends on the srslte_refsignal_t object for creating the LTE
 *                CSR signal.
 *
 *  Reference:
 *********************************************************************************************/

#ifndef CHEST_DL_
#define CHEST_DL_

#include <stdio.h>

#include "srslte/config.h"

#include "srslte/utils/debug.h"
#include "srslte/ch_estimation/chest_common.h"
#include "srslte/resampling/interp.h"
#include "srslte/ch_estimation/refsignal_dl.h"
#include "srslte/common/phy_common.h"
#include "srslte/sync/pss.h"
#include "srslte/sync/cfo.h"

// ****************** Definition of macros ***********************
#define ENABLE_CHEST_DL_PRINTS 0

#define CHEST_DL_PRINT(_fmt, ...) do { if(ENABLE_CHEST_DL_PRINTS && scatter_verbose_level >= 0) { \
  fprintf(stdout, "[CHEST DL PRINT]: " _fmt, __VA_ARGS__); } } while(0)

#define CHEST_DL_DEBUG(_fmt, ...) do { if(ENABLE_CHEST_DL_PRINTS && scatter_verbose_level >= SRSLTE_VERBOSE_DEBUG) { \
  fprintf(stdout, "[CHEST DL DEBUG]: " _fmt, __VA_ARGS__); } } while(0)

#define CHEST_DL_INFO(_fmt, ...) do { if(ENABLE_CHEST_DL_PRINTS && scatter_verbose_level >= SRSLTE_VERBOSE_INFO) { \
  fprintf(stdout, "[CHEST DL INFO]: " _fmt, __VA_ARGS__); } } while(0)

#define CHEST_DL_ERROR(_fmt, ...) do { fprintf(stdout, "[CHEST DL ERROR]: " _fmt, __VA_ARGS__); } while(0)

typedef enum {
  SRSLTE_NOISE_ALG_REFS,
  SRSLTE_NOISE_ALG_PSS,
  SRSLTE_NOISE_ALG_EMPTY,
} srslte_chest_dl_noise_alg_t;

typedef struct {
  srslte_cell_t cell;
  srslte_refsignal_cs_t csr_signal;
  cf_t *pilot_estimates;
  cf_t *pilot_estimates_average;
  cf_t *pilot_recv_signal;
  cf_t *tmp_noise;

#ifdef FREQ_SEL_SNR
  float snr_vector[12000];
  float pilot_power[12000];
#endif
  uint32_t smooth_filter_len;
  float smooth_filter[SRSLTE_CHEST_MAX_SMOOTH_FIL_LEN];

  srslte_interp_linsrslte_vec_t srslte_interp_linvec;
  srslte_interp_lin_t srslte_interp_lin;

  float rssi[SRSLTE_MAX_PORTS];
  float rsrp[SRSLTE_MAX_PORTS];
  float noise_estimate[SRSLTE_MAX_PORTS];

  /* Use PSS for noise estimation in LS linear interpolation mode */
  cf_t pss_signal[SRSLTE_PSS_LEN];
  cf_t tmp_pss[SRSLTE_PSS_LEN];
  cf_t tmp_pss_noisy[SRSLTE_PSS_LEN];

  srslte_chest_dl_noise_alg_t noise_alg;

  bool cfo_estimate_from_csr_enable;
  cf_t *tmp_cfo_estimate;
  float cfo_csr;
  srslte_cfo_t cfocorr;
  uint32_t fft_size;

  float n;
  float ns;
  float ng;
  uint32_t npilots;

} srslte_chest_dl_t;


SRSLTE_API int srslte_chest_dl_init(srslte_chest_dl_t *q,
                                    srslte_cell_t cell);

SRSLTE_API void srslte_chest_dl_free(srslte_chest_dl_t *q);

SRSLTE_API void srslte_chest_dl_set_smooth_filter(srslte_chest_dl_t *q,
                                                  float *filter,
                                                  uint32_t filter_len);

SRSLTE_API void srslte_chest_dl_set_smooth_filter3_coeff(srslte_chest_dl_t* q,
                                                         float w);

SRSLTE_API void srslte_chest_dl_set_noise_alg(srslte_chest_dl_t *q,
                                              srslte_chest_dl_noise_alg_t noise_estimation_alg);

SRSLTE_API int srslte_chest_dl_estimate(srslte_chest_dl_t *q,
                                        cf_t *input,
                                        cf_t *ce[SRSLTE_MAX_PORTS],
                                        uint32_t sf_idx);

SRSLTE_API int srslte_chest_dl_estimate_port(srslte_chest_dl_t *q,
                                             cf_t *input,
                                             cf_t *ce,
                                             uint32_t sf_idx,
                                             uint32_t port_id);

SRSLTE_API float srslte_chest_dl_get_noise_estimate(srslte_chest_dl_t *q);

SRSLTE_API float srslte_chest_dl_get_snr(srslte_chest_dl_t *q);

SRSLTE_API float srslte_chest_dl_get_rssi(srslte_chest_dl_t *q);

SRSLTE_API float srslte_chest_dl_get_rsrq(srslte_chest_dl_t *q);

SRSLTE_API float srslte_chest_dl_get_rsrp(srslte_chest_dl_t *q);

SRSLTE_API float chest_estimate_cfo(srslte_chest_dl_t *q);

SRSLTE_API float srslte_chest_dl_get_cfo(srslte_chest_dl_t *q);

SRSLTE_API void srslte_chest_dl_set_cfo_csr(srslte_chest_dl_t *q, bool flag);

#endif
