/**
 *
 * \section COPYRIGHT
 *
 * Copyright 2013-2014 The libLTE Developers. See the
 * COPYRIGHT file at the top-level directory of this distribution.
 *
 * \section LICENSE
 *
 * This file is part of the libLTE library.
 *
 * libLTE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libLTE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * A copy of the GNU Lesser General Public License can be found in
 * the LICENSE file in the top-level directory of this distribution
 * and at http://www.gnu.org/licenses/.
 *
 */

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include "srslte/common/phy_common.h"
#include "srslte/utils/bit.h"
#include "srslte/utils/vector.h"
#include "srslte/utils/debug.h"
#include "srslte/phch/ra.h"
#include "srslte/utils/bit.h"

#include "tbs_tables.h"

#define min(a,b) (a<b?a:b)

/* Returns the number of RE in a PRB in a slot and subframe */
uint32_t ra_re_x_prb(uint32_t subframe, uint32_t slot, uint32_t prb_idx, uint32_t nof_prb,
    uint32_t nof_ports, uint32_t nof_ctrl_symbols, srslte_cp_t cp) {

  uint32_t re;
  bool skip_refs = false;

  if (slot == 0) {
    re = (SRSLTE_CP_NSYMB(cp) - nof_ctrl_symbols) * SRSLTE_NRE;
  } else {
    re = SRSLTE_CP_NSYMB(cp) * SRSLTE_NRE;
  }

  /* if it's the prb in the middle, there are less RE due to PBCH and PSS/SSS */
  if ((subframe == 0 || subframe == 5)
      && (prb_idx >= nof_prb / 2 - 3 && prb_idx < nof_prb / 2 + 3)) {
    if (subframe == 0) {
      if (slot == 0) {
        re = (SRSLTE_CP_NSYMB(cp) - nof_ctrl_symbols - 2) * SRSLTE_NRE;
      } else {
        if (SRSLTE_CP_ISEXT(cp)) {
          re = (SRSLTE_CP_NSYMB(cp) - 4) * SRSLTE_NRE;
          skip_refs = true;
        } else {
          re = (SRSLTE_CP_NSYMB(cp) - 4) * SRSLTE_NRE + 2 * nof_ports;
        }
      }
    } else if (subframe == 5) {
      if (slot == 0) {
        re = (SRSLTE_CP_NSYMB(cp) - nof_ctrl_symbols - 2) * SRSLTE_NRE;
      }
    }
    if ((nof_prb % 2)
        && (prb_idx == nof_prb / 2 - 3 || prb_idx == nof_prb / 2 + 3)) {
      if (slot == 0) {
        re += 2 * SRSLTE_NRE / 2;
      } else if (subframe == 0) {
        re += 4 * SRSLTE_NRE / 2 - nof_ports;
        if (SRSLTE_CP_ISEXT(cp)) {
          re -= nof_ports > 2 ? 2 : nof_ports;
        }
      }
    }
  }

  // remove references
  if (!skip_refs) {
    switch (nof_ports) {
    case 1:
    case 2:
      re -= 2 * (slot + 1) * nof_ports;
      break;
    case 4:
      if (slot == 1) {
        re -= 12;
      } else {
        re -= 4;
        if (nof_ctrl_symbols == 1) {
          re -= 4;
        }
      }
      break;
    }
  }

  return re;
}

void srslte_ra_prb_fprint(FILE *f, srslte_ra_prb_slot_t *prb, uint32_t nof_prb) {
  int i;
  if (prb->nof_prb > 0) {
    for (i=0;i<nof_prb;i++) {
      if (prb->prb_idx[i]) {
        fprintf(f, "%d, ", i);
      }
    }
    fprintf(f, "\n");
  }
  
}

/** Compute PRB allocation for Uplink as defined in 8.1 and 8.4 of 36.213 */
int srslte_ra_ul_alloc(srslte_ra_ul_alloc_t *prb_dist, srslte_ra_pusch_t *ra, uint32_t n_rb_ho, uint32_t nof_prb) {
  
  bzero(prb_dist, sizeof(srslte_ra_ul_alloc_t));  
  prb_dist->L_prb = ra->type2_alloc.L_crb;
  uint32_t n_prb_1 = ra->type2_alloc.RB_start;
  uint32_t n_rb_pusch = 0;

  if (n_rb_ho%2) {
    n_rb_ho++;
  }
  
  if (ra->freq_hop_fl == SRSLTE_RA_PUSCH_HOP_DISABLED || ra->freq_hop_fl == SRSLTE_RA_PUSCH_HOP_TYPE2) {
    /* For no freq hopping or type2 freq hopping, n_prb is the same 
     * n_prb_tilde is calculated during resource mapping
     */
    for (uint32_t i=0;i<2;i++) {
      prb_dist->n_prb[i] = n_prb_1;        
    }
    if (ra->freq_hop_fl == SRSLTE_RA_PUSCH_HOP_DISABLED) {
      prb_dist->freq_hopping = 0;
    } else {
      prb_dist->freq_hopping = 2;      
    }
    INFO("prb1: %d, prb2: %d, L: %d\n", prb_dist->n_prb[0], prb_dist->n_prb[1], prb_dist->L_prb);
  } else {
    /* Type1 frequency hopping as defined in 8.4.1 of 36.213 
      * frequency offset between 1st and 2nd slot is fixed. 
      */
    n_rb_pusch = nof_prb - n_rb_ho - (nof_prb%2);
    
    // starting prb idx for slot 0 is as given by resource grant
    prb_dist->n_prb[0] = n_prb_1;
    if (n_prb_1 < n_rb_ho/2) {
      fprintf(stderr, "Invalid Frequency Hopping parameters. Offset: %d, n_prb_1: %d\n", n_rb_ho, n_prb_1);
    }
    uint32_t n_prb_1_tilde = n_prb_1;

    // prb idx for slot 1 
    switch(ra->freq_hop_fl) {
      case SRSLTE_RA_PUSCH_HOP_QUART:
        prb_dist->n_prb[1] = (n_rb_pusch/4+ n_prb_1_tilde)%n_rb_pusch;            
        break;
      case SRSLTE_RA_PUSCH_HOP_QUART_NEG:
        if (n_prb_1 < n_rb_pusch/4) {
          prb_dist->n_prb[1] = (n_rb_pusch+ n_prb_1_tilde -n_rb_pusch/4);                                
        } else {
          prb_dist->n_prb[1] = (n_prb_1_tilde -n_rb_pusch/4);                      
        }
        break;
      case SRSLTE_RA_PUSCH_HOP_HALF:
        prb_dist->n_prb[1] = (n_rb_pusch/2+ n_prb_1_tilde)%n_rb_pusch;            
        break;
      default:
        break;        
    }
    INFO("n_rb_pusch: %d, prb1: %d, prb2: %d, L: %d\n", n_rb_pusch, prb_dist->n_prb[0], prb_dist->n_prb[1], prb_dist->L_prb);
    prb_dist->freq_hopping = 1;
  }
  return SRSLTE_SUCCESS;
}

/* Computes the number of RE for each PRB in the prb_dist structure */
void srslte_ra_dl_alloc_re(srslte_ra_dl_alloc_t *prb_dist, uint32_t nof_prb, uint32_t nof_ports,
    uint32_t nof_ctrl_symbols, srslte_cp_t cp) {
  uint32_t i, j, s;

  /* Set start symbol according to Section 7.1.6.4 in 36.213 */
  prb_dist->lstart = nof_ctrl_symbols;
  // Compute number of RE per subframe
  for (i = 0; i < SRSLTE_NSUBFRAMES_X_FRAME; i++) {
    prb_dist->re_sf[i] = 0;
    for (s = 0; s < 2; s++) {
      for (j = 0; j < nof_prb; j++) {
        if (prb_dist->slot[s].prb_idx[j]) {
          prb_dist->re_sf[i] += ra_re_x_prb(i, s, j,
              nof_prb, nof_ports, nof_ctrl_symbols, cp);          
        }
      }
    }
  }
}

/** Compute PRB allocation for Downlink as defined in 7.1.6 of 36.213 */
int srslte_ra_dl_alloc(srslte_ra_dl_alloc_t *prb_dist, srslte_ra_pdsch_t *ra, uint32_t nof_prb) {
  int i, j;
  uint32_t bitmask;
  uint32_t P = srslte_ra_type0_P(nof_prb);
  uint32_t n_rb_rbg_subset, n_rb_type1;

  bzero(prb_dist, sizeof(srslte_ra_dl_alloc_t));
  switch (ra->alloc_type) {
  case SRSLTE_RA_ALLOC_TYPE0:
    bitmask = ra->type0_alloc.rbg_bitmask;
    int nb = (int) ceilf((float) nof_prb / P);
    for (i = 0; i < nb; i++) {
      if (bitmask & (1 << (nb - i - 1))) {
        for (j = 0; j < P; j++) {
          if (i*P+j < nof_prb) {
            prb_dist->slot[0].prb_idx[i * P + j] = true;
            prb_dist->slot[0].nof_prb++;
          }
        }
      }
    }
    memcpy(&prb_dist->slot[1], &prb_dist->slot[0], sizeof(srslte_ra_prb_slot_t));
    break;
  case SRSLTE_RA_ALLOC_TYPE1:
    n_rb_type1 = srslte_ra_type1_N_rb(nof_prb);
    if (ra->type1_alloc.rbg_subset < (nof_prb / P) % P) {
      n_rb_rbg_subset = ((nof_prb - 1) / (P * P)) * P + P;
    } else if (ra->type1_alloc.rbg_subset == ((nof_prb / P) % P)) {
      n_rb_rbg_subset = ((nof_prb - 1) / (P * P)) * P + ((nof_prb - 1) % P) + 1;
    } else {
      n_rb_rbg_subset = ((nof_prb - 1) / (P * P)) * P;
    }
    int shift = ra->type1_alloc.shift ? (n_rb_rbg_subset - n_rb_type1) : 0;
    bitmask = ra->type1_alloc.vrb_bitmask;
    for (i = 0; i < n_rb_type1; i++) {
      if (bitmask & (1 << (n_rb_type1 - i - 1))) {
        prb_dist->slot[0].prb_idx[((i + shift) / P)
            * P * P + ra->type1_alloc.rbg_subset * P + (i + shift) % P] = true;
        prb_dist->slot[0].nof_prb++;
      }
    }
    memcpy(&prb_dist->slot[1], &prb_dist->slot[0], sizeof(srslte_ra_prb_slot_t));
    break;
  case SRSLTE_RA_ALLOC_TYPE2:
    if (ra->type2_alloc.mode == SRSLTE_RA_TYPE2_LOC) {
      for (i = 0; i < ra->type2_alloc.L_crb; i++) {
        prb_dist->slot[0].prb_idx[i + ra->type2_alloc.RB_start] = true;
        prb_dist->slot[0].nof_prb++;
      }
      memcpy(&prb_dist->slot[1], &prb_dist->slot[0], sizeof(srslte_ra_prb_slot_t));
    } else {
      /* Mapping of Virtual to Physical RB for distributed type is defined in
       * 6.2.3.2 of 36.211
       */
      int N_gap, N_tilde_vrb, n_tilde_vrb, n_tilde_prb, n_tilde2_prb, N_null,
          N_row, n_vrb;
      int n_tilde_prb_odd, n_tilde_prb_even;
      if (ra->type2_alloc.n_gap == SRSLTE_RA_TYPE2_NG1) {
        N_tilde_vrb = srslte_ra_type2_n_vrb_dl(nof_prb, true);
        N_gap = srslte_ra_type2_ngap(nof_prb, true);
      } else {
        N_tilde_vrb = 2 * srslte_ra_type2_n_vrb_dl(nof_prb, true);
        N_gap = srslte_ra_type2_ngap(nof_prb, false);
      }
      N_row = (int) ceilf((float) N_tilde_vrb / (4 * P)) * P;
      N_null = 4 * N_row - N_tilde_vrb;
      for (i = 0; i < ra->type2_alloc.L_crb; i++) {
        n_vrb = i + ra->type2_alloc.RB_start;
        n_tilde_vrb = n_vrb % N_tilde_vrb;
        n_tilde_prb = 2 * N_row * (n_tilde_vrb % 2) + n_tilde_vrb / 2
            + N_tilde_vrb * (n_vrb / N_tilde_vrb);
        n_tilde2_prb = N_row * (n_tilde_vrb % 4) + n_tilde_vrb / 4
            + N_tilde_vrb * (n_vrb / N_tilde_vrb);

        if (N_null != 0 && n_tilde_vrb >= (N_tilde_vrb - N_null)
            && (n_tilde_vrb % 2) == 1) {
          n_tilde_prb_odd = n_tilde_prb - N_row;
        } else if (N_null != 0 && n_tilde_vrb >= (N_tilde_vrb - N_null)
            && (n_tilde_vrb % 2) == 0) {
          n_tilde_prb_odd = n_tilde_prb - N_row + N_null / 2;
        } else if (N_null != 0 && n_tilde_vrb < (N_tilde_vrb - N_null)
            && (n_tilde_vrb % 4) >= 2) {
          n_tilde_prb_odd = n_tilde2_prb - N_null / 2;
        } else {
          n_tilde_prb_odd = n_tilde2_prb;
        }
        n_tilde_prb_even = (n_tilde_prb_odd + N_tilde_vrb / 2) % N_tilde_vrb
            + N_tilde_vrb * (n_vrb / N_tilde_vrb);

        if (n_tilde_prb_odd < N_tilde_vrb / 2) {
          prb_dist->slot[0].prb_idx[n_tilde_prb_odd] = true;
        } else {
          prb_dist->slot[0].prb_idx[n_tilde_prb_odd + N_gap
              - N_tilde_vrb / 2] = true;
        }
        prb_dist->slot[0].nof_prb++;
        if (n_tilde_prb_even < N_tilde_vrb / 2) {
          prb_dist->slot[1].prb_idx[n_tilde_prb_even] = true;
        } else {
          prb_dist->slot[1].prb_idx[n_tilde_prb_even + N_gap
              - N_tilde_vrb / 2] = true;
        }
        prb_dist->slot[1].nof_prb++;
      }
    }
    break;
  default:
    return SRSLTE_ERROR;
  }

  return SRSLTE_SUCCESS;
}

/* Returns the number of allocated PRB for Uplink */
uint32_t srslte_ra_nprb_ul(srslte_ra_pusch_t *ra, uint32_t nof_prb) {
  return ra->type2_alloc.L_crb;
}

/* Returns the number of allocated PRB for Downlink */
uint32_t srslte_ra_nprb_dl(srslte_ra_pdsch_t *ra, uint32_t nof_prb) {
  uint32_t nprb;
  uint32_t nof_rbg, P;
  switch (ra->alloc_type) {
  case SRSLTE_RA_ALLOC_TYPE0:
    // Get the number of allocated RBG except the last RBG
    nof_rbg = srslte_bit_count(ra->type0_alloc.rbg_bitmask & 0xFFFFFFFE);
    P = srslte_ra_type0_P(nof_prb);
    if (nof_rbg > (uint32_t) ceilf((float) nof_prb / P)) {
      nof_rbg = (uint32_t) ceilf((float) nof_prb / P) - 1;
    }
    nprb = nof_rbg * P;

    // last RBG may have smaller size. Add if set
    uint32_t P_last = (nof_prb % P);
    if (!P_last)
      P_last = P;
    nprb += P_last * (ra->type0_alloc.rbg_bitmask & 1);
    break;
  case SRSLTE_RA_ALLOC_TYPE1:
    nprb = srslte_bit_count(ra->type1_alloc.vrb_bitmask);
    if (nprb > srslte_ra_type1_N_rb(nof_prb)) {
      fprintf(stderr, "Number of RB (%d) can not exceed %d\n", nprb,
          srslte_ra_type1_N_rb(nof_prb));
      return SRSLTE_ERROR;
    }
    break;
  case SRSLTE_RA_ALLOC_TYPE2:
    nprb = ra->type2_alloc.L_crb;
    break;
  default:
    return SRSLTE_ERROR;
  }
  return nprb;
}

/* RBG size for type0 scheduling as in table 7.1.6.1-1 of 36.213 */
uint32_t srslte_ra_type0_P(uint32_t nof_prb) {
  if (nof_prb <= 10) {
    return 1;
  } else if (nof_prb <= 26) {
    return 2;
  } else if (nof_prb <= 63) {
    return 3;
  } else {
    return 4;
  }
}

/* Returns N_rb_type1 according to section 7.1.6.2 */
uint32_t srslte_ra_type1_N_rb(uint32_t nof_prb) {
  uint32_t P = srslte_ra_type0_P(nof_prb);
  return (uint32_t) ceilf((float) nof_prb / P) - (uint32_t) ceilf(log2f((float) P)) - 1;
}

/* Convert Type2 scheduling L_crb and RB_start to RIV value */
uint32_t srslte_ra_type2_to_riv(uint32_t L_crb, uint32_t RB_start, uint32_t nof_prb) {
  uint32_t riv;
  if (L_crb <= nof_prb / 2) {
    riv = nof_prb * (L_crb - 1) + RB_start;
  } else {
    riv = nof_prb * (nof_prb - L_crb + 1) + nof_prb - 1 - RB_start;
  }
  return riv;
}

/* Convert Type2 scheduling RIV value to L_crb and RB_start values */
void srslte_ra_type2_from_riv(uint32_t riv, uint32_t *L_crb, uint32_t *RB_start,
    uint32_t nof_prb, uint32_t nof_vrb) {
  *L_crb = (uint32_t) (riv / nof_prb) + 1;
  *RB_start = (uint32_t) (riv % nof_prb);
  if (*L_crb > nof_vrb - *RB_start) {
    *L_crb = nof_prb - (int) (riv / nof_prb) + 1;
    *RB_start = nof_prb - riv % nof_prb - 1;
  }
}

/* Table 6.2.3.2-1 in 36.211 */
uint32_t srslte_ra_type2_ngap(uint32_t nof_prb, bool ngap_is_1) {
  if (nof_prb <= 10) {
    return nof_prb / 2;
  } else if (nof_prb == 11) {
    return 4;
  } else if (nof_prb <= 19) {
    return 8;
  } else if (nof_prb <= 26) {
    return 12;
  } else if (nof_prb <= 44) {
    return 18;
  } else if (nof_prb <= 49) {
    return 27;
  } else if (nof_prb <= 63) {
    return ngap_is_1 ? 27 : 9;
  } else if (nof_prb <= 79) {
    return ngap_is_1 ? 32 : 16;
  } else {
    return ngap_is_1 ? 48 : 16;
  }
}

/* Table 7.1.6.3-1 in 36.213 */
uint32_t srslte_ra_type2_n_rb_step(uint32_t nof_prb) {
  if (nof_prb < 50) {
    return 2;
  } else {
    return 4;
  }
}

/* as defined in 6.2.3.2 of 36.211 */
uint32_t srslte_ra_type2_n_vrb_dl(uint32_t nof_prb, bool ngap_is_1) {
  uint32_t ngap = srslte_ra_type2_ngap(nof_prb, ngap_is_1);
  if (ngap_is_1) {
    return 2 * (ngap < (nof_prb - ngap) ? ngap : nof_prb - ngap);
  } else {
    return ((uint32_t) nof_prb / ngap) * 2 * ngap;
  }
}

/* Converts MCS index to srslte_ra_mcs_t structure for Downlink as defined inTable 7.1.7.1-1 on 36.213 */
int srslte_ra_mcs_from_idx_dl(uint32_t mcs_idx, uint32_t nof_prb, srslte_ra_mcs_t *mcs) {
  if (mcs_idx < 10) {
    mcs->mod = SRSLTE_MOD_QPSK;
    mcs->tbs = srslte_ra_tbs_from_idx(mcs_idx, nof_prb);
  } else if (mcs_idx < 17) {
    mcs->mod = SRSLTE_MOD_16QAM;
    mcs->tbs = srslte_ra_tbs_from_idx(mcs_idx - 1, nof_prb);
  } else if (mcs_idx < 29) {
    mcs->mod = SRSLTE_MOD_64QAM;
    mcs->tbs = srslte_ra_tbs_from_idx(mcs_idx - 2, nof_prb);
  } else if (mcs_idx == 29) {
    mcs->mod = SRSLTE_MOD_QPSK;
    mcs->tbs = 0;
  } else if (mcs_idx == 30) {
    mcs->mod = SRSLTE_MOD_16QAM;
    mcs->tbs = 0;
  } else if (mcs_idx == 31) {
    mcs->mod = SRSLTE_MOD_64QAM;
    mcs->tbs = 0;
  } else {
    return SRSLTE_ERROR;
  }
  return SRSLTE_SUCCESS;
}

/* Converts MCS index to srslte_ra_mcs_t structure for Uplink as defined in Table 8.6.1-1 on 36.213 */
int srslte_ra_mcs_from_idx_ul(uint32_t mcs_idx, uint32_t nof_prb, srslte_ra_mcs_t *mcs) {
  if (mcs_idx < 11) {
    mcs->mod = SRSLTE_MOD_QPSK;
    mcs->tbs = srslte_ra_tbs_from_idx(mcs_idx, nof_prb);
  } else if (mcs_idx < 21) {
    mcs->mod = SRSLTE_MOD_16QAM;
    mcs->tbs = srslte_ra_tbs_from_idx(mcs_idx - 1, nof_prb);
  } else if (mcs_idx < 29) {
    mcs->mod = SRSLTE_MOD_64QAM;
    mcs->tbs = srslte_ra_tbs_from_idx(mcs_idx - 2, nof_prb);
  } else {
    return SRSLTE_ERROR;
  }
  return SRSLTE_SUCCESS;
}

/* Downlink Transport Block size for Format 1C as defined in 7.1.7.2.2-1 on 36.213 */
int srslte_ra_tbs_from_idx_format1c(uint32_t tbs_idx) {
  if (tbs_idx < 32) {
    return tbs_format1c_table[tbs_idx];
  } else {
    return SRSLTE_ERROR;
  }
}

/* Downlink Transport Block size determination as defined in 7.1.7.2 on 36.213 */
int srslte_ra_tbs_from_idx(uint32_t tbs_idx, uint32_t n_prb) {
  if (tbs_idx < 27 && n_prb > 0 && n_prb <= SRSLTE_MAX_PRB) {
    return tbs_table[tbs_idx][n_prb - 1];
  } else {
    return SRSLTE_ERROR;
  }
}

/* Returns lowest nearest index of TBS value in table 7.1.7.2 on 36.213
 * or -1 if the TBS value is not within the valid TBS values
 */
int srslte_ra_tbs_to_table_idx(uint32_t tbs, uint32_t n_prb) {
  uint32_t idx;
  if (n_prb > 0 && n_prb <= SRSLTE_MAX_PRB) {
    return SRSLTE_ERROR;
  }
  if (tbs < tbs_table[0][n_prb]) {
    return SRSLTE_ERROR;
  }
  for (idx = 1; idx < 28; idx++) {
    if (tbs_table[idx - 1][n_prb] <= tbs && tbs_table[idx][n_prb] >= tbs) {
      return idx;
    }
  }
  return SRSLTE_ERROR;
}

void srslte_ra_pusch_fprint(FILE *f, srslte_ra_pusch_t *ra, uint32_t nof_prb) {
  fprintf(f, " - Resource Allocation Type 2 mode :\t%s\n",
      ra->type2_alloc.mode == SRSLTE_RA_TYPE2_LOC ? "Localized" : "Distributed");
  
  fprintf(f, "   + Frequency Hopping:\t\t\t");
  if (ra->freq_hop_fl == SRSLTE_RA_PUSCH_HOP_DISABLED) {
    fprintf(f, "No\n");
  } else {
    fprintf(f, "Yes\n");
  }
  fprintf(f, "   + Resource Indicator Value:\t\t%d\n", ra->type2_alloc.riv);
  if (ra->type2_alloc.mode == SRSLTE_RA_TYPE2_LOC) {
  fprintf(f, "   + VRB Assignment:\t\t\t%d VRB starting with VRB %d\n",
    ra->type2_alloc.L_crb, ra->type2_alloc.RB_start);
  } else {
  fprintf(f, "   + VRB Assignment:\t\t\t%d VRB starting with VRB %d\n",
    ra->type2_alloc.L_crb, ra->type2_alloc.RB_start);
  fprintf(f, "   + VRB gap selection:\t\t\tGap %d\n",
    ra->type2_alloc.n_gap == SRSLTE_RA_TYPE2_NG1 ? 1 : 2);
  fprintf(f, "   + VRB gap:\t\t\t\t%d\n",
    srslte_ra_type2_ngap(nof_prb, ra->type2_alloc.n_gap == SRSLTE_RA_TYPE2_NG1));

  }
  
  fprintf(f, " - Number of PRBs:\t\t\t%d\n", srslte_ra_nprb_ul(ra, nof_prb));
  fprintf(f, " - Modulation and coding scheme index:\t%d\n", ra->mcs_idx);
  fprintf(f, " - Modulation type:\t\t\t%s\n", srslte_mod_string(ra->mcs.mod));
  fprintf(f, " - Transport block size:\t\t%d\n", ra->mcs.tbs);
  fprintf(f, " - New data indicator:\t\t\t%s\n", ra->ndi ? "Yes" : "No");
  fprintf(f, " - Redundancy version:\t\t\t%d\n", ra->rv_idx);
  fprintf(f, " - TPC command for PUCCH:\t\t--\n");    
}

char *ra_type_string(srslte_ra_type_t alloc_type) {
  switch (alloc_type) {
  case SRSLTE_RA_ALLOC_TYPE0:
    return "Type 0";
  case SRSLTE_RA_ALLOC_TYPE1:
    return "Type 1";
  case SRSLTE_RA_ALLOC_TYPE2:
    return "Type 2";
  default:
    return "N/A";
  }
}


void srslte_ra_pdsch_fprint(FILE *f, srslte_ra_pdsch_t *ra, uint32_t nof_prb) {
  fprintf(f, " - Resource Allocation Type:\t\t%s\n",
      ra_type_string(ra->alloc_type));
  switch (ra->alloc_type) {
  case SRSLTE_RA_ALLOC_TYPE0:
    fprintf(f, "   + Resource Block Group Size:\t\t%d\n", srslte_ra_type0_P(nof_prb));
    fprintf(f, "   + RBG Bitmap:\t\t\t0x%x\n", ra->type0_alloc.rbg_bitmask);
    break;
  case SRSLTE_RA_ALLOC_TYPE1:
    fprintf(f, "   + Resource Block Group Size:\t\t%d\n", srslte_ra_type0_P(nof_prb));
    fprintf(f, "   + RBG Bitmap:\t\t\t0x%x\n", ra->type1_alloc.vrb_bitmask);
    fprintf(f, "   + RBG Subset:\t\t\t%d\n", ra->type1_alloc.rbg_subset);
    fprintf(f, "   + RBG Shift:\t\t\t\t%s\n",
        ra->type1_alloc.shift ? "Yes" : "No");
    break;
  case SRSLTE_RA_ALLOC_TYPE2:
    fprintf(f, "   + Type:\t\t\t\t%s\n",
        ra->type2_alloc.mode == SRSLTE_RA_TYPE2_LOC ? "Localized" : "Distributed");
    fprintf(f, "   + Resource Indicator Value:\t\t%d\n", ra->type2_alloc.riv);
    if (ra->type2_alloc.mode == SRSLTE_RA_TYPE2_LOC) {
      fprintf(f, "   + VRB Assignment:\t\t\t%d VRB starting with VRB %d\n",
          ra->type2_alloc.L_crb, ra->type2_alloc.RB_start);
    } else {
      fprintf(f, "   + VRB Assignment:\t\t\t%d VRB starting with VRB %d\n",
          ra->type2_alloc.L_crb, ra->type2_alloc.RB_start);
      fprintf(f, "   + VRB gap selection:\t\t\tGap %d\n",
          ra->type2_alloc.n_gap == SRSLTE_RA_TYPE2_NG1 ? 1 : 2);
      fprintf(f, "   + VRB gap:\t\t\t\t%d\n",
          srslte_ra_type2_ngap(nof_prb, ra->type2_alloc.n_gap == SRSLTE_RA_TYPE2_NG1));
    }
    break;
  }

  srslte_ra_dl_alloc_t alloc;
  srslte_ra_dl_alloc(&alloc, ra, nof_prb);
  for (int s = 0; s < 2; s++) {
    fprintf(f, " - PRB Bitmap Assignment %dst slot:\n", s);
    srslte_ra_prb_fprint(f, &alloc.slot[s], nof_prb);
  }

  fprintf(f, " - Number of PRBs:\t\t\t%d\n", srslte_ra_nprb_dl(ra, nof_prb));
  fprintf(f, " - Modulation and coding scheme index:\t%d\n", ra->mcs_idx);
  fprintf(f, " - Modulation type:\t\t\t%s\n", srslte_mod_string(ra->mcs.mod));
  fprintf(f, " - Transport block size:\t\t%d\n", ra->mcs.tbs);
  fprintf(f, " - HARQ process:\t\t\t%d\n", ra->harq_process);
  fprintf(f, " - New data indicator:\t\t\t%s\n", ra->ndi ? "Yes" : "No");
  fprintf(f, " - Redundancy version:\t\t\t%d\n", ra->rv_idx);
  fprintf(f, " - TPC command for PUCCH:\t\t--\n");
}

