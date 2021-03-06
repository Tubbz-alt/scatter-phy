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

#include "srslte/ue/ue_dl.h"

#include <complex.h>
#include <math.h>
#include <string.h>

#define CURRENT_FFTSIZE   srslte_symbol_sz(q->cell.nof_prb)
#define CURRENT_SFLEN     SRSLTE_SF_LEN(CURRENT_FFTSIZE)

#define CURRENT_SLOTLEN_RE SRSLTE_SLOT_LEN_RE(q->cell.nof_prb, q->cell.cp)
#define CURRENT_SFLEN_RE SRSLTE_SF_LEN_RE(q->cell.nof_prb, q->cell.cp)

static srslte_dci_format_t ue_formats[] = {SRSLTE_DCI_FORMAT1A, SRSLTE_DCI_FORMAT1}; // Only TM1 and TM2 are currently supported
const uint32_t nof_ue_formats = 2;

static srslte_dci_format_t common_formats[] = {SRSLTE_DCI_FORMAT1A,SRSLTE_DCI_FORMAT1C};
const uint32_t nof_common_formats = 2;

// This define is used to enable or disable the usage of the last successful DCI to decode a subframe where DCI was not found.
#define ENABLE_USE_OF_LAST_SUFRAME 0
static srslte_dci_msg_t last_dci_msg;
static srslte_ra_mcs_t last_ra_mcs;

int srslte_ue_dl_init(srslte_ue_dl_t *q, srslte_cell_t cell) {
  return srslte_ue_dl_init_generic(q, cell, 0, false);
}

int srslte_ue_dl_init_generic(srslte_ue_dl_t *q, srslte_cell_t cell, uint32_t phy_id, bool add_sch_to_front) {
  int ret = SRSLTE_ERROR_INVALID_INPUTS;

  if(q                 != NULL &&
     srslte_cell_isvalid(&cell))
  {
    ret = SRSLTE_ERROR;

    bzero(q, sizeof(srslte_ue_dl_t));

    q->cell = cell;
    q->pkt_errors = 0;
    q->pkts_total = 0;
    q->nof_detected = 0;
    q->pending_ul_dci_rnti = 0;
    q->sample_offset = 0;
    q->wrong_decoding_counter = 0;
    q->decoded_cfi = 0;
    q->found_dci = 0;
    q->noise_estimate = 0.0;
    q->expected_cfi = 1;
    q->decode_pdcch = true;
    q->phy_id = phy_id;
    q->add_sch_to_front = add_sch_to_front;
    q->pss_len = SRSLTE_PSS_LEN;

    if(srslte_ofdm_rx_init(&q->fft, q->cell.cp, q->cell.nof_prb)) {
      fprintf(stderr, "Error initiating FFT\n");
      goto clean_exit;
    }
    if(srslte_chest_dl_init(&q->chest, cell)) {
      fprintf(stderr, "Error initiating channel estimator\n");
      goto clean_exit;
    }
    if(srslte_regs_init(&q->regs, q->cell)) {
      fprintf(stderr, "Error initiating REGs\n");
      goto clean_exit;
    }
    if(srslte_pcfich_init(&q->pcfich, &q->regs, q->cell)) {
      fprintf(stderr, "Error creating PCFICH object\n");
      goto clean_exit;
    }
    if(srslte_phich_init(&q->phich, &q->regs, q->cell)) {
      fprintf(stderr, "Error creating PHICH object\n");
      goto clean_exit;
    }
    if(srslte_pdcch_init(&q->pdcch, &q->regs, q->cell)) {
      fprintf(stderr, "Error creating PDCCH object\n");
      goto clean_exit;
    }
    if(srslte_pdsch_init_generic(&q->pdsch, q->cell, q->phy_id, q->add_sch_to_front)) {
      fprintf(stderr, "Error creating PDSCH object\n");
      goto clean_exit;
    }
    if(srslte_softbuffer_rx_init_scatter(&q->softbuffer, q->cell.nof_prb)) {
      fprintf(stderr, "Error initiating soft buffer\n");
      goto clean_exit;
    }
    if(srslte_cfo_init(&q->sfo_correct, q->cell.nof_prb*SRSLTE_NRE)) {
      fprintf(stderr, "Error initiating SFO correct\n");
      goto clean_exit;
    }
    srslte_cfo_set_tol(&q->sfo_correct, 1e-5/q->fft.symbol_sz);

    q->sf_symbols = srslte_vec_malloc(CURRENT_SFLEN_RE * sizeof(cf_t));
    if(!q->sf_symbols) {
      perror("malloc");
      goto clean_exit;
    }
    for(uint32_t i=0;i<q->cell.nof_ports;i++) {
      q->ce[i] = srslte_vec_malloc(CURRENT_SFLEN_RE * sizeof(cf_t));
      if(!q->ce[i]) {
        perror("malloc");
        goto clean_exit;
      }
    }

    ret = SRSLTE_SUCCESS;
  } else {
    fprintf(stderr, "Invalid cell properties: Id=%d, Ports=%d, PRBs=%d\n",
            cell.id, cell.nof_ports, cell.nof_prb);
  }

clean_exit:
  if(ret == SRSLTE_ERROR) {
    srslte_ue_dl_free(q);
  }
  return ret;
}

void srslte_ue_dl_free(srslte_ue_dl_t *q) {
  if(q) {
    srslte_ofdm_rx_free(&q->fft);
    srslte_chest_dl_free(&q->chest);
    srslte_regs_free(&q->regs);
    srslte_pcfich_free(&q->pcfich);
    srslte_phich_free(&q->phich);
    srslte_pdcch_free(&q->pdcch);
    srslte_pdsch_free(&q->pdsch);
    srslte_cfo_free(&q->sfo_correct);
    srslte_softbuffer_rx_free_scatter(&q->softbuffer);
    if(q->sf_symbols) {
      free(q->sf_symbols);
    }
    for(uint32_t i = 0; i < q->cell.nof_ports; i++) {
      if(q->ce[i]) {
        free(q->ce[i]);
      }
    }
    bzero(q, sizeof(srslte_ue_dl_t));
  }
}

void srslte_ue_dl_set_expected_cfi(srslte_ue_dl_t *q, uint32_t expected_cfi) {
  q->expected_cfi = expected_cfi;
}

/* Precalculate the PDSCH scramble sequences for a given RNTI. This function takes a while
 * to execute, so shall be called once the final C-RNTI has been allocated for the session.
 * For the connection procedure, use srslte_pusch_encode_rnti() or srslte_pusch_decode_rnti() functions
 */
void srslte_ue_dl_set_rnti(srslte_ue_dl_t *q, uint16_t rnti) {
  srslte_pdsch_set_rnti(&q->pdsch, rnti);

  // Compute UE-specific and Common search space for this RNTI
  for (int cfi=0;cfi<3;cfi++) {
    for (int sf_idx=0;sf_idx<10;sf_idx++) {
      q->current_ss_ue[cfi][sf_idx].nof_locations = srslte_pdcch_ue_locations(&q->pdcch, q->current_ss_ue[cfi][sf_idx].loc, MAX_CANDIDATES_UE, sf_idx, cfi+1, rnti);
    }
    q->current_ss_common[cfi].nof_locations = srslte_pdcch_common_locations(&q->pdcch, q->current_ss_common[cfi].loc, MAX_CANDIDATES_COM, cfi+1);
  }

  q->current_rnti = rnti;
}

void srslte_ue_dl_reset(srslte_ue_dl_t *q) {
  srslte_softbuffer_rx_reset(&q->softbuffer);
  bzero(&q->pdsch_cfg, sizeof(srslte_pdsch_cfg_t));
}

void srslte_ue_dl_set_sample_offset(srslte_ue_dl_t * q, float sample_offset) {
  q->sample_offset = sample_offset;
}

/** Applies the following operations to a subframe of synchronized samples:
 *    - OFDM demodulation
 *    - Channel estimation
 *    - PCFICH decoding
 *    - PDCCH decoding: Find DCI for RNTI given by previous call to srslte_ue_dl_set_rnti()
 *    - PDSCH decoding: Decode TB scrambling with RNTI given by srslte_ue_dl_set_rnti()
 */
int srslte_ue_dl_decode(srslte_ue_dl_t *q, cf_t *input, uint8_t *data, uint32_t tti) {
  return srslte_ue_dl_decode_rnti(q, input, data, tti, q->current_rnti);
}

int srslte_ue_dl_decode_fft_estimate(srslte_ue_dl_t *q, cf_t *input, uint32_t sf_idx, uint32_t *cfi, float *cfi_corr) {
  if(input && q && cfi && sf_idx < SRSLTE_NSUBFRAMES_X_FRAME) {

    /* Run FFT for all subframe data */
    srslte_ofdm_rx_sf(&q->fft, input, q->sf_symbols);

    /* Correct SFO multiplying by complex exponential in the time domain */
    if(q->sample_offset) {
      for(int i = 0; i < 2*SRSLTE_CP_NSYMB(q->cell.cp); i++) {
        srslte_cfo_correct(&q->sfo_correct,
                         &q->sf_symbols[i*q->cell.nof_prb*SRSLTE_NRE],
                         &q->sf_symbols[i*q->cell.nof_prb*SRSLTE_NRE],
                         q->sample_offset / q->fft.symbol_sz);
      }
    }
    return srslte_ue_dl_decode_estimate(q, sf_idx, cfi, cfi_corr);
  } else {
    return SRSLTE_ERROR_INVALID_INPUTS;
  }
}

int srslte_ue_dl_decode_estimate(srslte_ue_dl_t *q, uint32_t sf_idx, uint32_t *cfi, float *cfi_corr) {
  if(q && cfi && sf_idx < SRSLTE_NSUBFRAMES_X_FRAME) {

    // Get channel estimates for each port.
    srslte_chest_dl_estimate(&q->chest, q->sf_symbols, q->ce, sf_idx);

    if(q->decode_pdcch) {
      // First decode PCFICH and obtain CFI.
      if(srslte_pcfich_decode(&q->pcfich, q->sf_symbols, q->ce,
                               srslte_chest_dl_get_noise_estimate(&q->chest),
                               sf_idx, cfi, cfi_corr) < 0) {
        fprintf(stderr, "Error decoding PCFICH\n");
        return SRSLTE_ERROR;
      }

      INFO("Decoded CFI=%d with correlation %.2f, sf_idx=%d\n", *cfi, *cfi_corr, sf_idx);

      if(srslte_regs_set_cfi(&q->regs, *cfi)) {
        fprintf(stderr, "Error setting CFI\n");
        return SRSLTE_ERROR;
      }
    } else {
      *cfi = q->expected_cfi;
      *cfi_corr = 1000;
    }

    return SRSLTE_SUCCESS;
  } else {
    return SRSLTE_ERROR_INVALID_INPUTS;
  }
}

int srslte_ue_dl_cfg_grant(srslte_ue_dl_t *q, srslte_ra_dl_grant_t *grant, uint32_t cfi, uint32_t sf_idx, uint32_t rvidx) {
  return srslte_pdsch_cfg(&q->pdsch_cfg, q->cell, grant, cfi, sf_idx, rvidx);
}

void srslte_ue_dl_set_max_noi(srslte_ue_dl_t *q, uint32_t max_iterations) {
  srslte_pdsch_set_max_noi(&q->pdsch, max_iterations);
}

float srslte_ul_dl_average_noi(srslte_ue_dl_t *q) {
  return srslte_pdsch_average_noi(&q->pdsch);
}

uint32_t srslte_ue_dl_last_noi(srslte_ue_dl_t *q) {
  return srslte_pdsch_last_noi(&q->pdsch);
}

int srslte_ue_dl_decode_rnti(srslte_ue_dl_t *q, cf_t *input, uint8_t *data, uint32_t tti, uint16_t rnti) {
  float cfi_corr;
  srslte_dci_msg_t dci_msg;
  srslte_ra_dl_dci_t dci_unpacked;
  srslte_ra_dl_grant_t grant;
  int ret = SRSLTE_ERROR;

  // Get the time when decoding started.
  clock_gettime(CLOCK_REALTIME, &q->decoding_start_timestamp);

  uint32_t sf_idx = tti%10;

  q->pkts_total++; // Total of subframes that were synchronized and aligned, might include miss detections.

  if((ret = srslte_ue_dl_decode_fft_estimate(q, input, sf_idx, &q->decoded_cfi, &cfi_corr)) < 0) {
    return ret;
  }

  // If decoded CFI does not correspond to the expected one there is no point in continuing with the decoding.
  if(q->decoded_cfi != q->expected_cfi) {
    UE_DL_DEBUG("Decoded wrong CFI = %d (should be %d) with correlation %.2f, sf_idx = %d\n", q->decoded_cfi, q->expected_cfi, cfi_corr, sf_idx);
    q->found_dci = 0;
    q->noise_estimate = srslte_chest_dl_get_noise_estimate(&q->chest);
    // If the code reachs this point it means one subframe was detected and aligned but CFI is different from the expected value.
    q->wrong_decoding_counter++;
    return 0;
  }

  if(srslte_pdcch_extract_llr(&q->pdcch, q->sf_symbols, q->ce, srslte_chest_dl_get_noise_estimate(&q->chest), sf_idx, q->decoded_cfi)) {
    UE_DL_ERROR("Error extracting LLRs\n",0);
    return SRSLTE_ERROR;
  }

  q->found_dci = srslte_ue_dl_find_dl_dci(q, q->decoded_cfi, sf_idx, rnti, &dci_msg);
  // If DCI is not found but we already decoded a subframe, then, we try to use the last DCI to decode the data.
  if(ENABLE_USE_OF_LAST_SUFRAME && q->nof_detected > 0 && q->found_dci == 0) {
    memcpy((uint8_t*)&dci_msg, (uint8_t*)&last_dci_msg, sizeof(srslte_dci_msg_t));
    q->found_dci = 1;
  }
  if(q->found_dci == 1) {

    if(srslte_dci_msg_to_dl_grant(&dci_msg, rnti, q->cell.nof_prb, q->cell.nof_ports, &dci_unpacked, &grant)) {
      UE_DL_ERROR("Error unpacking DCI\n",0);
      return SRSLTE_ERROR;
    }

    /* ===== These lines of code are supposed to be MAC functionality === */

    uint32_t rvidx = 0;
    if(dci_unpacked.rv_idx < 0) {
      uint32_t sfn = tti/10;
      uint32_t k   = (sfn/2)%4;
      rvidx        = ((uint32_t) ceilf((float)1.5*k))%4;
      srslte_softbuffer_rx_reset_tbs(&q->softbuffer, grant.mcs.tbs);
    } else {
      rvidx = dci_unpacked.rv_idx;
      srslte_softbuffer_rx_reset_tbs(&q->softbuffer, grant.mcs.tbs);
    }

    if(srslte_ue_dl_cfg_grant(q, &grant, q->decoded_cfi, sf_idx, rvidx)) {
      return SRSLTE_ERROR;
    }

    /* ===== End of MAC functionality ========== */

    // Uncoment next line to do ZF by default in pdsch_ue example
    //float noise_estimate = 0;
    q->noise_estimate = srslte_chest_dl_get_noise_estimate(&q->chest);

    if(q->pdsch_cfg.grant.mcs.mod > 0 && q->pdsch_cfg.grant.mcs.tbs >= 0) {
      ret = srslte_pdsch_decode_rnti(&q->pdsch, &q->pdsch_cfg, &q->softbuffer,
                                    q->sf_symbols, q->ce,
                                    q->noise_estimate,
                                    rnti, data);
      if(ret == SRSLTE_ERROR) {
        q->pkt_errors++; // CFI is equal to the expected value and DCI was found but data was not correctly decoded.
      } else if(ret == SRSLTE_ERROR_INVALID_INPUTS) {
        UE_DL_ERROR("Error calling srslte_pdsch_decode()\n",0);
      }
    }
  } else {
    // If the code reachs this point it means one subframe was detected and aligned but DCI was not found.
    q->wrong_decoding_counter++;
  }

  if(q->found_dci == 1 && ret == SRSLTE_SUCCESS) {
    q->nof_detected++; // If CFI is equal to expected, DCI is found and data is correctly decoded, then we increment the number of correctly decoded packets.
    // Store last found DCI.
    if(ENABLE_USE_OF_LAST_SUFRAME && (q->pdsch_cfg.grant.mcs.mod != last_ra_mcs.mod || q->pdsch_cfg.grant.mcs.tbs != last_ra_mcs.tbs || q->pdsch_cfg.grant.mcs.idx != last_ra_mcs.idx)) {
      memcpy((uint8_t*)&last_ra_mcs, (uint8_t*)&q->pdsch_cfg.grant.mcs, sizeof(srslte_ra_mcs_t));
      memcpy((uint8_t*)&last_dci_msg, (uint8_t*)&dci_msg, sizeof(srslte_dci_msg_t));
    }
    return q->pdsch_cfg.grant.mcs.tbs;  // Transport Block (TB) size in bits.
  } else {
    return 0;
  }
}

uint32_t srslte_ue_dl_get_ncce(srslte_ue_dl_t *q) {
  return q->last_location.ncce;
}

static int dci_blind_search(srslte_ue_dl_t *q, dci_blind_search_t *search_space, uint16_t rnti, srslte_dci_msg_t *dci_msg)
{
  int ret = SRSLTE_ERROR;
  uint16_t crc_rem = 0;
  if (rnti) {
    ret = 0;
    int i=0;
    while (!ret && i < search_space->nof_locations) {
      DEBUG("Searching format %s in %d,%d (%d/%d)\n",
             srslte_dci_format_string(search_space->format), search_space->loc[i].ncce, search_space->loc[i].L,
             i, search_space->nof_locations);

      if (srslte_pdcch_decode_msg(&q->pdcch, dci_msg, &search_space->loc[i], search_space->format, &crc_rem)) {
        fprintf(stderr, "Error decoding DCI msg\n");
        return SRSLTE_ERROR;
      }
      if (crc_rem == rnti) {
        // If searching for Format1A but found Format0 save it for later
        if (dci_msg->format == SRSLTE_DCI_FORMAT0 && search_space->format == SRSLTE_DCI_FORMAT1A)
        {
          q->pending_ul_dci_rnti = crc_rem;
          memcpy(&q->pending_ul_dci_msg, dci_msg, sizeof(srslte_dci_msg_t));
          memcpy(&q->last_location_ul, &search_space->loc[i], sizeof(srslte_dci_location_t));
        // Else if we found it, save location and leave
        } else if (dci_msg->format == search_space->format) {
          ret = 1;
          if (dci_msg->format == SRSLTE_DCI_FORMAT0) {
            memcpy(&q->last_location_ul, &search_space->loc[i], sizeof(srslte_dci_location_t));
          } else {
            memcpy(&q->last_location, &search_space->loc[i], sizeof(srslte_dci_location_t));
          }
        }
      }
      i++;
    }
  } else {
    fprintf(stderr, "RNTI not specified\n");
  }
  return ret;
}

int srslte_ue_dl_find_ul_dci(srslte_ue_dl_t *q, uint32_t cfi, uint32_t sf_idx, uint16_t rnti, srslte_dci_msg_t *dci_msg)
{
  if (rnti) {
    /* Do not search if an UL DCI is already pending */
    if (q->pending_ul_dci_rnti == rnti) {
      q->pending_ul_dci_rnti = 0;
      memcpy(dci_msg, &q->pending_ul_dci_msg, sizeof(srslte_dci_msg_t));
      return 1;
    }

    // Configure and run DCI blind search
    dci_blind_search_t search_space;
    dci_blind_search_t *current_ss = &search_space;
    if (q->current_rnti == rnti) {
      current_ss = &q->current_ss_ue[cfi-1][sf_idx];
    } else {
      // If locations are not pre-generated, generate them now
      current_ss->nof_locations = srslte_pdcch_ue_locations(&q->pdcch, current_ss->loc, MAX_CANDIDATES_UE, sf_idx, cfi, rnti);
    }

    srslte_pdcch_set_cfi(&q->pdcch, cfi);

    current_ss->format = SRSLTE_DCI_FORMAT0;
    INFO("Searching UL C-RNTI in %d ue locations\n", search_space.nof_locations);
    return dci_blind_search(q, current_ss, rnti, dci_msg);
  } else {
    return 0;
  }
}

int srslte_ue_dl_find_dl_dci(srslte_ue_dl_t *q, uint32_t cfi, uint32_t sf_idx, uint16_t rnti, srslte_dci_msg_t *dci_msg)
{
  srslte_rnti_type_t rnti_type;
  if (rnti == SRSLTE_SIRNTI) {
    rnti_type = SRSLTE_RNTI_SI;
  } else if (rnti == SRSLTE_PRNTI) {
    rnti_type = SRSLTE_RNTI_PCH;
  } else if (rnti <= SRSLTE_RARNTI_END) {
    rnti_type = SRSLTE_RNTI_RAR;
  } else {
    rnti_type = SRSLTE_RNTI_USER;
  }
  return srslte_ue_dl_find_dl_dci_type(q, cfi, sf_idx, rnti, rnti_type, dci_msg);
}

// Blind search for SI/P/RA-RNTI
static int find_dl_dci_type_siprarnti(srslte_ue_dl_t *q, uint32_t cfi, uint16_t rnti, srslte_dci_msg_t *dci_msg)
{
  int ret = 0;
  // Configure and run DCI blind search
  dci_blind_search_t search_space;
  search_space.nof_locations = srslte_pdcch_common_locations(&q->pdcch, search_space.loc, MAX_CANDIDATES_COM, cfi);
  INFO("Searching SI/P/RA-RNTI in %d common locations, %d formats\n", search_space.nof_locations, nof_common_formats);
  // Search for RNTI only if there is room for the common search space
  if (search_space.nof_locations > 0) {
    for (int f=0;f<nof_common_formats;f++) {
      search_space.format = common_formats[f];
      if ((ret = dci_blind_search(q, &search_space, rnti, dci_msg))) {
        return ret;
      }
    }
  }
  return SRSLTE_SUCCESS;
}

// Blind search for C-RNTI
static int find_dl_dci_type_crnti(srslte_ue_dl_t *q, uint32_t cfi, uint32_t sf_idx, uint16_t rnti, srslte_dci_msg_t *dci_msg)
{
  int ret = SRSLTE_SUCCESS;
  dci_blind_search_t search_space;
  dci_blind_search_t *current_ss = &search_space;

  // Search UE-specific search space
  if (q->current_rnti == rnti) {
    current_ss = &q->current_ss_ue[cfi-1][sf_idx];
  } else {
    // If locations are not pre-generated, generate them now
    current_ss->nof_locations = srslte_pdcch_ue_locations(&q->pdcch, current_ss->loc, MAX_CANDIDATES_UE, sf_idx, cfi, rnti);
  }

  srslte_pdcch_set_cfi(&q->pdcch, cfi);

  INFO("Searching DL C-RNTI in %d ue locations, %d formats\n", current_ss->nof_locations, nof_ue_formats);
  for (int f=0;f<nof_ue_formats;f++) {
    current_ss->format = ue_formats[f];
    if ((ret = dci_blind_search(q, current_ss, rnti, dci_msg))) {
      return ret;
    }
  }

  // Search Format 1A in the Common SS also
  if (q->current_rnti == rnti) {
    current_ss = &q->current_ss_common[cfi-1];
  } else {
    // If locations are not pre-generated, generate them now
    current_ss->nof_locations = srslte_pdcch_common_locations(&q->pdcch, current_ss->loc, MAX_CANDIDATES_COM, cfi);
  }

  srslte_pdcch_set_cfi(&q->pdcch, cfi);

  // Search for RNTI only if there is room for the common search space
  if (current_ss->nof_locations > 0) {
    current_ss->format = SRSLTE_DCI_FORMAT1A;
    INFO("Searching DL C-RNTI in %d ue locations, format 1A\n", current_ss->nof_locations);
    return dci_blind_search(q, current_ss, rnti, dci_msg);
  }
  return SRSLTE_SUCCESS;
}

int srslte_ue_dl_find_dl_dci_type(srslte_ue_dl_t *q, uint32_t cfi, uint32_t sf_idx,
                                  uint16_t rnti, srslte_rnti_type_t rnti_type, srslte_dci_msg_t *dci_msg)
{
  if (rnti_type == SRSLTE_RNTI_SI || rnti_type == SRSLTE_RNTI_PCH || rnti_type == SRSLTE_RNTI_RAR) {
    return find_dl_dci_type_siprarnti(q, cfi, rnti, dci_msg);
  } else {
    return find_dl_dci_type_crnti(q, cfi, sf_idx, rnti, dci_msg);
  }
}

bool srslte_ue_dl_decode_phich(srslte_ue_dl_t *q, uint32_t sf_idx, uint32_t n_prb_lowest, uint32_t n_dmrs)
{
  uint8_t ack_bit;
  float distance;
  uint32_t ngroup, nseq;
  srslte_phich_calc(&q->phich, n_prb_lowest, n_dmrs, &ngroup, &nseq);
  INFO("Decoding PHICH sf_idx=%d, n_prb_lowest=%d, n_dmrs=%d, n_group=%d, n_seq=%d, Ngroups=%d, Nsf=%d\n",
    sf_idx, n_prb_lowest, n_dmrs, ngroup, nseq,
    srslte_phich_ngroups(&q->phich), srslte_phich_nsf(&q->phich));
  if (!srslte_phich_decode(&q->phich, q->sf_symbols, q->ce, 0, ngroup, nseq, sf_idx, &ack_bit, &distance)) {
    INFO("Decoded PHICH %d with distance %f\n", ack_bit, distance);
  } else {
    fprintf(stderr, "Error decoding PHICH\n");
    return false;
  }
  if (ack_bit) {
    return true;
  } else {
    return false;
  }
}

void srslte_ue_dl_save_signal(srslte_ue_dl_t *q, srslte_softbuffer_rx_t *softbuffer, uint32_t tti, uint32_t rv_idx, uint16_t rnti, uint32_t cfi) {
  srslte_vec_save_file("sf_symbols", q->sf_symbols, SRSLTE_SF_LEN_RE(q->cell.nof_prb, q->cell.cp)*sizeof(cf_t));
  printf("%d samples\n", SRSLTE_SF_LEN_RE(q->cell.nof_prb, q->cell.cp));
  srslte_vec_save_file("ce0", q->ce[0], SRSLTE_SF_LEN_RE(q->cell.nof_prb, q->cell.cp)*sizeof(cf_t));
  if (q->cell.nof_ports > 1) {
    srslte_vec_save_file("ce1", q->ce[1], SRSLTE_SF_LEN_RE(q->cell.nof_prb, q->cell.cp)*sizeof(cf_t));
  }
  srslte_vec_save_file("pcfich_ce0", q->pcfich.ce[0], q->pcfich.nof_symbols*sizeof(cf_t));
  srslte_vec_save_file("pcfich_ce1", q->pcfich.ce[1], q->pcfich.nof_symbols*sizeof(cf_t));
  srslte_vec_save_file("pcfich_symbols", q->pcfich.symbols[0], q->pcfich.nof_symbols*sizeof(cf_t));
  srslte_vec_save_file("pcfich_eq_symbols", q->pcfich.d, q->pcfich.nof_symbols*sizeof(cf_t));
  srslte_vec_save_file("pcfich_llr", q->pcfich.data_f, PCFICH_CFI_LEN*sizeof(float));

  srslte_vec_save_file("pdcch_ce0", q->pdcch.ce[0], q->pdcch.nof_cce*36*sizeof(cf_t));
  srslte_vec_save_file("pdcch_ce1", q->pdcch.ce[1], q->pdcch.nof_cce*36*sizeof(cf_t));
  srslte_vec_save_file("pdcch_symbols", q->pdcch.symbols[0], q->pdcch.nof_cce*36*sizeof(cf_t));
  srslte_vec_save_file("pdcch_eq_symbols", q->pdcch.d, q->pdcch.nof_cce*36*sizeof(cf_t));
  srslte_vec_save_file("pdcch_llr", q->pdcch.llr, q->pdcch.nof_cce*72*sizeof(float));


  srslte_vec_save_file("pdsch_symbols", q->pdsch.d, q->pdsch_cfg.nbits.nof_re*sizeof(cf_t));
  srslte_vec_save_file("llr", q->pdsch.e, q->pdsch_cfg.nbits.nof_bits*sizeof(cf_t));
  int cb_len = q->pdsch_cfg.cb_segm.K1;
  for (int i=0;i<q->pdsch_cfg.cb_segm.C;i++) {
    char tmpstr[64];
    snprintf(tmpstr,64,"rmout_%d.dat",i);
    srslte_vec_save_file(tmpstr, softbuffer->buffer_f[i], (3*cb_len+12)*sizeof(int16_t));
  }
  printf("Saved files for tti=%d, sf=%d, cfi=%d, mcs=%d, rv=%d, rnti=%d\n", tti, tti%10, cfi,
         q->pdsch_cfg.grant.mcs.idx, rv_idx, rnti);
}

void srslte_ue_dl_set_cfo_csr(srslte_ue_dl_t *q, bool flag) {
  srslte_chest_dl_set_cfo_csr(&q->chest, flag);
}

//******************************************************************************
//***************** Scatter system customized implementations ******************
//******************************************************************************
void srslte_ue_dl_set_decode_pdcch(srslte_ue_dl_t *q, bool flag) {
  q->decode_pdcch = flag;
}

void srslte_ue_dl_set_pss_length(srslte_ue_dl_t *q, uint32_t pss_len) {
  q->pss_len = pss_len;
}

unsigned int srslte_ue_dl_reverse(register unsigned int x) {
  x = (((x & 0xaaaaaaaa) >> 1) | ((x & 0x55555555) << 1));
  x = (((x & 0xcccccccc) >> 2) | ((x & 0x33333333) << 2));
  x = (((x & 0xf0f0f0f0) >> 4) | ((x & 0x0f0f0f0f) << 4));
  x = (((x & 0xff00ff00) >> 8) | ((x & 0x00ff00ff) << 8));
  return((x >> 16) | (x << 16));
}

uint32_t srslte_ue_dl_prbset_to_bitmask(uint32_t nof_prb) {
  uint32_t mask = 0;
  int nb = (int) ceilf((float) nof_prb / srslte_ra_type0_P(nof_prb));
  for(int i = 0; i < nb; i++) {
    if(i >= 0 && i < nb) {
      mask = mask | (0x1<<i);
    }
  }
  return srslte_ue_dl_reverse(mask)>>(32-nb);
}

void srslte_ue_dl_update_radl(srslte_ra_dl_dci_t *ra_dl, uint32_t mcs, uint32_t nof_prb) {
  bzero(ra_dl, sizeof(srslte_ra_dl_dci_t));
  ra_dl->harq_process = 0;
  ra_dl->mcs_idx = mcs;
  ra_dl->ndi = 0;
  ra_dl->rv_idx = 0;
  ra_dl->alloc_type = SRSLTE_RA_ALLOC_TYPE0;
  ra_dl->type0_alloc.rbg_bitmask = srslte_ue_dl_prbset_to_bitmask(nof_prb);
}

int srslte_ue_dl_decode_scatter(srslte_ue_dl_t *q, cf_t *input, uint8_t *data, uint32_t tti, uint32_t mcs) {
  return srslte_ue_dl_decode_rnti_scatter(q, input, data, tti, q->current_rnti, mcs);
}

int srslte_ue_dl_decode_rnti_scatter(srslte_ue_dl_t *q, cf_t *input, uint8_t *data, uint32_t tti, uint16_t rnti, int mcs) {
  float cfi_corr;
  srslte_ra_dl_dci_t ra_dl;
  srslte_dci_msg_t dci_msg;
  srslte_ra_dl_dci_t dci_unpacked;
  srslte_ra_dl_grant_t grant;
  int ret = SRSLTE_ERROR;

  // Get the time when decoding started.
  clock_gettime(CLOCK_REALTIME, &q->decoding_start_timestamp);

  uint32_t sf_idx = tti%10;

  q->pkts_total++; // Total of subframes that were synchronized and aligned, might include miss detections.

  if((ret = srslte_ue_dl_decode_fft_estimate(q, input, sf_idx, &q->decoded_cfi, &cfi_corr)) < 0) {
    UE_DL_ERROR("Error estimating channel or detecting CFI.\n", 0);
    return ret;
  }

  if(q->decode_pdcch) {
    // If decoded CFI does not correspond to the expected one there is no point in continuing with the decoding.
    if(q->decoded_cfi != q->expected_cfi) {
      UE_DL_DEBUG("Decoded wrong CFI = %d (should be %d) with correlation %.2f, sf_idx = %d\n", q->decoded_cfi, q->expected_cfi, cfi_corr, sf_idx);
      q->found_dci = 0;
      q->noise_estimate = srslte_chest_dl_get_noise_estimate(&q->chest);
      // If the code reachs this point it means one subframe was detected and aligned but CFI is different from the expected value.
      q->wrong_decoding_counter++;
      return 0;
    }

    if(srslte_pdcch_extract_llr(&q->pdcch, q->sf_symbols, q->ce, srslte_chest_dl_get_noise_estimate(&q->chest), sf_idx, q->decoded_cfi)) {
      UE_DL_ERROR("Error extracting LLRs\n",0);
      return SRSLTE_ERROR;
    }

    q->found_dci = srslte_ue_dl_find_dl_dci(q, q->decoded_cfi, sf_idx, rnti, &dci_msg);

    // If DCI is not found but we already decoded a subframe, then, we try to use the last DCI to decode the data.
    if(ENABLE_USE_OF_LAST_SUFRAME && q->nof_detected > 0 && q->found_dci == 0) {
      memcpy((uint8_t*)&dci_msg, (uint8_t*)&last_dci_msg, sizeof(srslte_dci_msg_t));
      q->found_dci = 1;
    }
  } else {
    q->found_dci = 1;
    srslte_ue_dl_update_radl(&ra_dl, mcs, q->cell.nof_prb);
    srslte_dci_msg_pack_pdsch(&ra_dl, SRSLTE_DCI_FORMAT1, &dci_msg, q->cell.nof_prb, false);
  }

  if(q->found_dci == 1) {

    // Convert DCI message into DL grant.
    if(srslte_dci_msg_to_dl_grant_scatter(&dci_msg, rnti, q->cell.nof_prb, q->cell.nof_ports, &dci_unpacked, &grant)) {
      UE_DL_ERROR("Error unpacking DCI\n",0);
      return SRSLTE_ERROR;
    }

    /* ===== These lines of code are supposed to be MAC functionality === */

    uint32_t rvidx = 0;
    if(dci_unpacked.rv_idx < 0) {
      uint32_t sfn = tti/10;
      uint32_t k   = (sfn/2)%4;
      rvidx        = ((uint32_t) ceilf((float)1.5*k))%4;
      srslte_softbuffer_rx_reset_tbs(&q->softbuffer, grant.mcs.tbs);
    } else {
      rvidx = dci_unpacked.rv_idx;
      srslte_softbuffer_rx_reset_tbs(&q->softbuffer, grant.mcs.tbs);
    }

    if(srslte_ue_dl_cfg_grant_scatter(q, &grant, q->decoded_cfi, sf_idx, rvidx)) {
      return SRSLTE_ERROR;
    }

    /* ===== End of MAC functionality ========== */

    // Uncoment next line to do ZF by default in pdsch_ue example
    //float noise_estimate = 0;
    q->noise_estimate = srslte_chest_dl_get_noise_estimate(&q->chest);

    if(q->pdsch_cfg.grant.mcs.mod > 0 && q->pdsch_cfg.grant.mcs.tbs >= 0) {
      ret = srslte_pdsch_decode_rnti_scatter(&q->pdsch, &q->pdsch_cfg, &q->softbuffer,
                                             q->sf_symbols, q->ce,
                                             q->noise_estimate,
                                             rnti, data);
      if(ret == SRSLTE_ERROR) {
        q->pkt_errors++; // CFI is equal to the expected value and DCI was found but data was not correctly decoded.
      } else if(ret == SRSLTE_ERROR_INVALID_INPUTS) {
        UE_DL_ERROR("Error calling srslte_pdsch_decode()\n",0);
      }
    }
  } else {
    // If the code reachs this point it means one subframe was detected and aligned but DCI was not found.
    q->wrong_decoding_counter++;
  }

  if(q->found_dci == 1 && ret == SRSLTE_SUCCESS) {
    q->nof_detected++; // If CFI is equal to expected, DCI is found and data is correctly decoded, then we increment the number of correctly decoded packets.
    // Store last found DCI.
    if(ENABLE_USE_OF_LAST_SUFRAME && (q->pdsch_cfg.grant.mcs.mod != last_ra_mcs.mod || q->pdsch_cfg.grant.mcs.tbs != last_ra_mcs.tbs || q->pdsch_cfg.grant.mcs.idx != last_ra_mcs.idx)) {
      memcpy((uint8_t*)&last_ra_mcs, (uint8_t*)&q->pdsch_cfg.grant.mcs, sizeof(srslte_ra_mcs_t));
      memcpy((uint8_t*)&last_dci_msg, (uint8_t*)&dci_msg, sizeof(srslte_dci_msg_t));
    }
    return q->pdsch_cfg.grant.mcs.tbs;  // Transport Block (TB) size in bits.
  } else {
    return 0;
  }
}

int srslte_ue_dl_cfg_grant_scatter(srslte_ue_dl_t *q, srslte_ra_dl_grant_t *grant, uint32_t cfi, uint32_t sf_idx, uint32_t rvidx) {
  return srslte_pdsch_cfg_scatter(&q->pdsch_cfg, q->add_sch_to_front, q->pss_len, q->cell, grant, cfi, sf_idx, rvidx);
}
