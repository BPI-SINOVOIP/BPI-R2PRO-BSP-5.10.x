/*
 *
 * Copyright 2015 Rockchip Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * @file       hal_h265d_reg.h
 * @brief
 * @author      csy(csy@rock-chips.com)

 * @version     1.0.0
 * @history
 *   2015.7.15 : Create
 */

#ifndef __HAL_H265D_REG_H__
#define __HAL_H265D_REG_H__

#include "rk_type.h"
#include "mpp_log.h"

#define HEVC_DECODER_REG_NUM        (48)
#define RKVDEC_REG_PERF_CYCLE_INDEX (64)

#define RKVDEC_HEVC_REGISTERS       (68)
#define RKVDEC_V1_REGISTERS         (78)
#define V345_HEVC_REGISTERS         (108)

typedef struct RKV_HEVC_REG_END {
    RK_U32        performance_cycle; //65
    RK_U32        axi_ddr_rdata;
    RK_U32        axi_ddr_wdata;
    RK_U32        fpgadebug_reset;
    RK_U32        reserve[9];
    RK_U32        extern_error_en;
} rkv_reg_end;

typedef struct V345_HEVC_REG_END {
    struct hevc_mvc0 {
        RK_U32    refp_layer_same_with_cur : 16  ;
        RK_U32    reserve           : 16  ;
    } reg064_mvc0;
    RK_U32 reserve[55];
} v345_reg_end;

typedef struct {
    struct swreg_id {
        RK_U32    minor_ver           : 8  ;
        RK_U32    major_ver           : 8  ;
        RK_U32    prod_num            : 16 ;
    } sw_id;

    struct swreg_int {
        RK_U32    sw_dec_e            : 1  ;
        RK_U32    sw_dec_clkgate_e    : 1  ;
        RK_U32    reserve0            : 2  ;
        RK_U32    sw_dec_irq_dis      : 1  ;
        RK_U32    sw_dec_timeout_e    : 1  ;
        RK_U32    sw_buf_empty_en     : 1  ;
        RK_U32    reserve1            : 1  ;
        RK_U32    sw_dec_irq          : 1  ;
        RK_U32    sw_dec_irq_raw      : 1  ;
        RK_U32    reserve2            : 2  ;
        RK_U32    sw_dec_rdy_sta      : 1  ;
        RK_U32    sw_dec_bus_sta      : 1  ;
        RK_U32    sw_dec_error_sta    : 1  ;
        RK_U32    sw_dec_timeout_sta  : 1  ;
        RK_U32    sw_dec_empty_sta    : 1  ;
        RK_U32    reserve4            : 3  ;
        RK_U32    sw_softrst_en_p     : 1  ;
        RK_U32    sw_force_softreset_valid: 1 ;
        RK_U32    sw_softreset_rdy    : 1  ;
        RK_U32    sw_wr_ddr_align_en  : 1;
        RK_U32    sw_scl_down_en      : 1;
        RK_U32    sw_allow_not_wr_unref_bframe : 1;
    } sw_interrupt; ///<- zrh: do nothing in C Model

    struct swreg_sysctrl {
        RK_U32    sw_in_endian        : 1  ;
        RK_U32    sw_in_swap32_e      : 1  ;
        RK_U32    sw_in_swap64_e      : 1  ;
        RK_U32    sw_str_endian       : 1  ;
        RK_U32    sw_str_swap32_e     : 1  ;
        RK_U32    sw_str_swap64_e     : 1  ;
        RK_U32    sw_out_endian       : 1  ;
        RK_U32    sw_out_swap32_e     : 1  ;
        RK_U32    sw_out_cbcr_swap    : 1  ;
        RK_U32    sw_error_info_en    : 1  ;
        RK_U32    sw_rlc_mode_direct_write : 1;
        RK_U32    sw_rlc_mode         : 1  ;
        RK_U32    sw_strm_start_bit   : 7  ;
        RK_U32    sw_inter_error_prc_mode : 1;
        RK_U32    sw_dec_mode         : 2  ;
        RK_U32    sw_info_collect_en  : 1  ;
        RK_U32    sw_wait_reset_en    : 1  ;
        RK_U32    sw_h26x_rps_mode    : 1  ;
        RK_U32    reserve2            : 5  ;
        RK_U32    sw_colmv_mode       : 1  ;
        RK_U32    sw_head_prior_high_en : 1;
    } sw_sysctrl;

    struct swreg_pic {
        RK_U32    sw_y_hor_virstride  : 9 ;
        RK_U32    reserve             : 3 ;
        RK_U32    sw_uv_hor_virstride : 9 ;
        RK_U32    sw_slice_num        : 8 ;
    } sw_picparameter;

    RK_U32        sw_strm_rlc_base        ;///<- zrh: do nothing in C Model
    RK_U32        sw_stream_len           ;///<- zrh: do nothing in C Model
    RK_U32        sw_cabactbl_base        ;///<- zrh: do nothing in C Model
    RK_U32        sw_decout_base          ;
    RK_U32        sw_y_virstride          ;
    RK_U32        sw_yuv_virstride        ;
    RK_U32        sw_refer_base[15]       ;
    RK_S32        sw_refer_poc[15]        ;
    RK_S32        sw_cur_poc              ;
    RK_U32        sw_rlcwrite_base        ;///<- zrh: do nothing in C Model
    RK_U32        sw_pps_base             ;///<- zrh: do nothing in C Model
    RK_U32        sw_rps_base             ;///<- zrh: do nothing in C Model
    RK_U32        cabac_error_en          ;///<- zrh add
    RK_U32        cabac_error_status      ;///<- zrh add

    struct cabac_error_ctu      {
        RK_U32   sw_cabac_error_ctu_xoffset    : 8;
        RK_U32   sw_cabac_error_ctu_yoffset    : 8;
        RK_U32   sw_streamfifo_space2full      : 7;
        RK_U32   reversed0                     : 9;
    } cabac_error_ctu;

    struct sao_ctu_position     {
        RK_U32   sw_saowr_xoffset              : 9;
        RK_U32   reversed0                     : 7;
        RK_U32   sw_saowr_yoffset             : 10;
        RK_U32   reversed1                     : 6;
    } sao_ctu_position;

    RK_U32        reg_not_use0[RKVDEC_REG_PERF_CYCLE_INDEX - HEVC_DECODER_REG_NUM];
    union {
        rkv_reg_end rkv_reg_ends;
        v345_reg_end v345_reg_ends;
    };
} H265d_REGS_t;

#endif
