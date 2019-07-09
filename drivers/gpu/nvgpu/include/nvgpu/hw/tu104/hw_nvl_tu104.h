/*
 * Copyright (c) 2018-2019, NVIDIA CORPORATION.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/*
 * Function/Macro naming determines intended use:
 *
 *     <x>_r(void) : Returns the offset for register <x>.
 *
 *     <x>_o(void) : Returns the offset for element <x>.
 *
 *     <x>_w(void) : Returns the word offset for word (4 byte) element <x>.
 *
 *     <x>_<y>_s(void) : Returns size of field <y> of register <x> in bits.
 *
 *     <x>_<y>_f(u32 v) : Returns a value based on 'v' which has been shifted
 *         and masked to place it at field <y> of register <x>.  This value
 *         can be |'d with others to produce a full register value for
 *         register <x>.
 *
 *     <x>_<y>_m(void) : Returns a mask for field <y> of register <x>.  This
 *         value can be ~'d and then &'d to clear the value of field <y> for
 *         register <x>.
 *
 *     <x>_<y>_<z>_f(void) : Returns the constant value <z> after being shifted
 *         to place it at field <y> of register <x>.  This value can be |'d
 *         with others to produce a full register value for <x>.
 *
 *     <x>_<y>_v(u32 r) : Returns the value of field <y> from a full register
 *         <x> value 'r' after being shifted to place its LSB at bit 0.
 *         This value is suitable for direct comparison with other unshifted
 *         values appropriate for use in field <y> of register <x>.
 *
 *     <x>_<y>_<z>_v(void) : Returns the constant value for <z> defined for
 *         field <y> of register <x>.  This value is suitable for direct
 *         comparison with unshifted values appropriate for use in field <y>
 *         of register <x>.
 */
#ifndef NVGPU_HW_NVL_TU104_H
#define NVGPU_HW_NVL_TU104_H

#include <nvgpu/types.h>
#include <nvgpu/safe_ops.h>

#define nvl_link_state_r()                                         (0x00000000U)
#define nvl_link_state_state_f(v)                            (((v)&0xffU) << 0U)
#define nvl_link_state_state_m()                              (U32(0xffU) << 0U)
#define nvl_link_state_state_v(r)                          (((r) >> 0U) & 0xffU)
#define nvl_link_state_state_init_v()                              (0x00000000U)
#define nvl_link_state_state_init_f()                                     (0x0U)
#define nvl_link_state_state_hwcfg_v()                             (0x00000001U)
#define nvl_link_state_state_hwcfg_f()                                    (0x1U)
#define nvl_link_state_state_swcfg_v()                             (0x00000002U)
#define nvl_link_state_state_swcfg_f()                                    (0x2U)
#define nvl_link_state_state_active_v()                            (0x00000003U)
#define nvl_link_state_state_active_f()                                   (0x3U)
#define nvl_link_state_state_fault_v()                             (0x00000004U)
#define nvl_link_state_state_fault_f()                                    (0x4U)
#define nvl_link_state_state_rcvy_ac_v()                           (0x00000008U)
#define nvl_link_state_state_rcvy_ac_f()                                  (0x8U)
#define nvl_link_state_state_rcvy_sw_v()                           (0x00000009U)
#define nvl_link_state_state_rcvy_sw_f()                                  (0x9U)
#define nvl_link_state_state_rcvy_rx_v()                           (0x0000000aU)
#define nvl_link_state_state_rcvy_rx_f()                                  (0xaU)
#define nvl_link_state_an0_busy_f(v)                         (((v)&0x1U) << 12U)
#define nvl_link_state_an0_busy_m()                           (U32(0x1U) << 12U)
#define nvl_link_state_an0_busy_v(r)                       (((r) >> 12U) & 0x1U)
#define nvl_link_state_tl_busy_f(v)                          (((v)&0x1U) << 13U)
#define nvl_link_state_tl_busy_m()                            (U32(0x1U) << 13U)
#define nvl_link_state_tl_busy_v(r)                        (((r) >> 13U) & 0x1U)
#define nvl_link_state_dbg_substate_f(v)                  (((v)&0xffffU) << 16U)
#define nvl_link_state_dbg_substate_m()                    (U32(0xffffU) << 16U)
#define nvl_link_state_dbg_substate_v(r)                (((r) >> 16U) & 0xffffU)
#define nvl_link_activity_r()                                      (0x0000000cU)
#define nvl_link_activity_blkact_f(v)                         (((v)&0x7U) << 0U)
#define nvl_link_activity_blkact_m()                           (U32(0x7U) << 0U)
#define nvl_link_activity_blkact_v(r)                       (((r) >> 0U) & 0x7U)
#define nvl_sublink_activity_r(i)\
		(nvgpu_safe_add_u32(0x00000010U, nvgpu_safe_mult_u32((i), 4U)))
#define nvl_sublink_activity_blkact0_f(v)                     (((v)&0x7U) << 0U)
#define nvl_sublink_activity_blkact0_m()                       (U32(0x7U) << 0U)
#define nvl_sublink_activity_blkact0_v(r)                   (((r) >> 0U) & 0x7U)
#define nvl_sublink_activity_blkact1_f(v)                     (((v)&0x7U) << 8U)
#define nvl_sublink_activity_blkact1_m()                       (U32(0x7U) << 8U)
#define nvl_sublink_activity_blkact1_v(r)                   (((r) >> 8U) & 0x7U)
#define nvl_link_config_r()                                        (0x00000018U)
#define nvl_link_config_ac_safe_en_f(v)                      (((v)&0x1U) << 30U)
#define nvl_link_config_ac_safe_en_m()                        (U32(0x1U) << 30U)
#define nvl_link_config_ac_safe_en_v(r)                    (((r) >> 30U) & 0x1U)
#define nvl_link_config_ac_safe_en_on_v()                          (0x00000001U)
#define nvl_link_config_ac_safe_en_on_f()                          (0x40000000U)
#define nvl_link_config_link_en_f(v)                         (((v)&0x1U) << 31U)
#define nvl_link_config_link_en_m()                           (U32(0x1U) << 31U)
#define nvl_link_config_link_en_v(r)                       (((r) >> 31U) & 0x1U)
#define nvl_link_config_link_en_on_v()                             (0x00000001U)
#define nvl_link_config_link_en_on_f()                             (0x80000000U)
#define nvl_link_change_r()                                        (0x00000040U)
#define nvl_link_change_oldstate_mask_f(v)                   (((v)&0xfU) << 16U)
#define nvl_link_change_oldstate_mask_m()                     (U32(0xfU) << 16U)
#define nvl_link_change_oldstate_mask_v(r)                 (((r) >> 16U) & 0xfU)
#define nvl_link_change_oldstate_mask_dontcare_v()                 (0x0000000fU)
#define nvl_link_change_oldstate_mask_dontcare_f()                    (0xf0000U)
#define nvl_link_change_newstate_f(v)                         (((v)&0xfU) << 4U)
#define nvl_link_change_newstate_m()                           (U32(0xfU) << 4U)
#define nvl_link_change_newstate_v(r)                       (((r) >> 4U) & 0xfU)
#define nvl_link_change_newstate_hwcfg_v()                         (0x00000001U)
#define nvl_link_change_newstate_hwcfg_f()                               (0x10U)
#define nvl_link_change_newstate_swcfg_v()                         (0x00000002U)
#define nvl_link_change_newstate_swcfg_f()                               (0x20U)
#define nvl_link_change_newstate_active_v()                        (0x00000003U)
#define nvl_link_change_newstate_active_f()                              (0x30U)
#define nvl_link_change_action_f(v)                           (((v)&0x3U) << 2U)
#define nvl_link_change_action_m()                             (U32(0x3U) << 2U)
#define nvl_link_change_action_v(r)                         (((r) >> 2U) & 0x3U)
#define nvl_link_change_action_ltssm_change_v()                    (0x00000001U)
#define nvl_link_change_action_ltssm_change_f()                           (0x4U)
#define nvl_link_change_status_f(v)                           (((v)&0x3U) << 0U)
#define nvl_link_change_status_m()                             (U32(0x3U) << 0U)
#define nvl_link_change_status_v(r)                         (((r) >> 0U) & 0x3U)
#define nvl_link_change_status_done_v()                            (0x00000000U)
#define nvl_link_change_status_done_f()                                   (0x0U)
#define nvl_link_change_status_busy_v()                            (0x00000001U)
#define nvl_link_change_status_busy_f()                                   (0x1U)
#define nvl_link_change_status_fault_v()                           (0x00000002U)
#define nvl_link_change_status_fault_f()                                  (0x2U)
#define nvl_sublink_change_r()                                     (0x00000044U)
#define nvl_sublink_change_countdown_f(v)                  (((v)&0xfffU) << 20U)
#define nvl_sublink_change_countdown_m()                    (U32(0xfffU) << 20U)
#define nvl_sublink_change_countdown_v(r)                (((r) >> 20U) & 0xfffU)
#define nvl_sublink_change_oldstate_mask_f(v)                (((v)&0xfU) << 16U)
#define nvl_sublink_change_oldstate_mask_m()                  (U32(0xfU) << 16U)
#define nvl_sublink_change_oldstate_mask_v(r)              (((r) >> 16U) & 0xfU)
#define nvl_sublink_change_oldstate_mask_dontcare_v()              (0x0000000fU)
#define nvl_sublink_change_oldstate_mask_dontcare_f()                 (0xf0000U)
#define nvl_sublink_change_sublink_f(v)                      (((v)&0xfU) << 12U)
#define nvl_sublink_change_sublink_m()                        (U32(0xfU) << 12U)
#define nvl_sublink_change_sublink_v(r)                    (((r) >> 12U) & 0xfU)
#define nvl_sublink_change_sublink_tx_v()                          (0x00000000U)
#define nvl_sublink_change_sublink_tx_f()                                 (0x0U)
#define nvl_sublink_change_sublink_rx_v()                          (0x00000001U)
#define nvl_sublink_change_sublink_rx_f()                              (0x1000U)
#define nvl_sublink_change_newstate_f(v)                      (((v)&0xfU) << 4U)
#define nvl_sublink_change_newstate_m()                        (U32(0xfU) << 4U)
#define nvl_sublink_change_newstate_v(r)                    (((r) >> 4U) & 0xfU)
#define nvl_sublink_change_newstate_hs_v()                         (0x00000000U)
#define nvl_sublink_change_newstate_hs_f()                                (0x0U)
#define nvl_sublink_change_newstate_eighth_v()                     (0x00000004U)
#define nvl_sublink_change_newstate_eighth_f()                           (0x40U)
#define nvl_sublink_change_newstate_train_v()                      (0x00000005U)
#define nvl_sublink_change_newstate_train_f()                            (0x50U)
#define nvl_sublink_change_newstate_safe_v()                       (0x00000006U)
#define nvl_sublink_change_newstate_safe_f()                             (0x60U)
#define nvl_sublink_change_newstate_off_v()                        (0x00000007U)
#define nvl_sublink_change_newstate_off_f()                              (0x70U)
#define nvl_sublink_change_action_f(v)                        (((v)&0x3U) << 2U)
#define nvl_sublink_change_action_m()                          (U32(0x3U) << 2U)
#define nvl_sublink_change_action_v(r)                      (((r) >> 2U) & 0x3U)
#define nvl_sublink_change_action_slsm_change_v()                  (0x00000001U)
#define nvl_sublink_change_action_slsm_change_f()                         (0x4U)
#define nvl_sublink_change_status_f(v)                        (((v)&0x3U) << 0U)
#define nvl_sublink_change_status_m()                          (U32(0x3U) << 0U)
#define nvl_sublink_change_status_v(r)                      (((r) >> 0U) & 0x3U)
#define nvl_sublink_change_status_done_v()                         (0x00000000U)
#define nvl_sublink_change_status_done_f()                                (0x0U)
#define nvl_sublink_change_status_busy_v()                         (0x00000001U)
#define nvl_sublink_change_status_busy_f()                                (0x1U)
#define nvl_sublink_change_status_fault_v()                        (0x00000002U)
#define nvl_sublink_change_status_fault_f()                               (0x2U)
#define nvl_link_test_r()                                          (0x00000048U)
#define nvl_link_test_mode_f(v)                               (((v)&0x1U) << 0U)
#define nvl_link_test_mode_m()                                 (U32(0x1U) << 0U)
#define nvl_link_test_mode_v(r)                             (((r) >> 0U) & 0x1U)
#define nvl_link_test_mode_enable_v()                              (0x00000001U)
#define nvl_link_test_mode_enable_f()                                     (0x1U)
#define nvl_link_test_auto_hwcfg_f(v)                        (((v)&0x1U) << 30U)
#define nvl_link_test_auto_hwcfg_m()                          (U32(0x1U) << 30U)
#define nvl_link_test_auto_hwcfg_v(r)                      (((r) >> 30U) & 0x1U)
#define nvl_link_test_auto_hwcfg_enable_v()                        (0x00000001U)
#define nvl_link_test_auto_hwcfg_enable_f()                        (0x40000000U)
#define nvl_link_test_auto_nvhs_f(v)                         (((v)&0x1U) << 31U)
#define nvl_link_test_auto_nvhs_m()                           (U32(0x1U) << 31U)
#define nvl_link_test_auto_nvhs_v(r)                       (((r) >> 31U) & 0x1U)
#define nvl_link_test_auto_nvhs_enable_v()                         (0x00000001U)
#define nvl_link_test_auto_nvhs_enable_f()                         (0x80000000U)
#define nvl_sl0_slsm_status_tx_r()                                 (0x00002024U)
#define nvl_sl0_slsm_status_tx_substate_f(v)                  (((v)&0xfU) << 0U)
#define nvl_sl0_slsm_status_tx_substate_m()                    (U32(0xfU) << 0U)
#define nvl_sl0_slsm_status_tx_substate_v(r)                (((r) >> 0U) & 0xfU)
#define nvl_sl0_slsm_status_tx_substate_stable_v()                 (0x00000000U)
#define nvl_sl0_slsm_status_tx_substate_stable_f()                        (0x0U)
#define nvl_sl0_slsm_status_tx_primary_state_f(v)             (((v)&0xfU) << 4U)
#define nvl_sl0_slsm_status_tx_primary_state_m()               (U32(0xfU) << 4U)
#define nvl_sl0_slsm_status_tx_primary_state_v(r)           (((r) >> 4U) & 0xfU)
#define nvl_sl0_slsm_status_tx_primary_state_hs_v()                (0x00000000U)
#define nvl_sl0_slsm_status_tx_primary_state_hs_f()                       (0x0U)
#define nvl_sl0_slsm_status_tx_primary_state_eighth_v()            (0x00000004U)
#define nvl_sl0_slsm_status_tx_primary_state_eighth_f()                  (0x40U)
#define nvl_sl0_slsm_status_tx_primary_state_train_v()             (0x00000005U)
#define nvl_sl0_slsm_status_tx_primary_state_train_f()                   (0x50U)
#define nvl_sl0_slsm_status_tx_primary_state_off_v()               (0x00000007U)
#define nvl_sl0_slsm_status_tx_primary_state_off_f()                     (0x70U)
#define nvl_sl0_slsm_status_tx_primary_state_safe_v()              (0x00000006U)
#define nvl_sl0_slsm_status_tx_primary_state_safe_f()                    (0x60U)
#define nvl_sl0_slsm_status_tx_primary_state_unknown_v()           (0x0000000dU)
#define nvl_sl0_slsm_status_tx_primary_state_unknown_f()                 (0xd0U)
#define nvl_sl1_slsm_status_rx_r()                                 (0x00003014U)
#define nvl_sl1_slsm_status_rx_substate_f(v)                  (((v)&0xfU) << 0U)
#define nvl_sl1_slsm_status_rx_substate_m()                    (U32(0xfU) << 0U)
#define nvl_sl1_slsm_status_rx_substate_v(r)                (((r) >> 0U) & 0xfU)
#define nvl_sl1_slsm_status_rx_substate_stable_v()                 (0x00000000U)
#define nvl_sl1_slsm_status_rx_substate_stable_f()                        (0x0U)
#define nvl_sl1_slsm_status_rx_primary_state_f(v)             (((v)&0xfU) << 4U)
#define nvl_sl1_slsm_status_rx_primary_state_m()               (U32(0xfU) << 4U)
#define nvl_sl1_slsm_status_rx_primary_state_v(r)           (((r) >> 4U) & 0xfU)
#define nvl_sl1_slsm_status_rx_primary_state_hs_v()                (0x00000000U)
#define nvl_sl1_slsm_status_rx_primary_state_hs_f()                       (0x0U)
#define nvl_sl1_slsm_status_rx_primary_state_eighth_v()            (0x00000004U)
#define nvl_sl1_slsm_status_rx_primary_state_eighth_f()                  (0x40U)
#define nvl_sl1_slsm_status_rx_primary_state_train_v()             (0x00000005U)
#define nvl_sl1_slsm_status_rx_primary_state_train_f()                   (0x50U)
#define nvl_sl1_slsm_status_rx_primary_state_off_v()               (0x00000007U)
#define nvl_sl1_slsm_status_rx_primary_state_off_f()                     (0x70U)
#define nvl_sl1_slsm_status_rx_primary_state_safe_v()              (0x00000006U)
#define nvl_sl1_slsm_status_rx_primary_state_safe_f()                    (0x60U)
#define nvl_sl1_slsm_status_rx_primary_state_unknown_v()           (0x0000000dU)
#define nvl_sl1_slsm_status_rx_primary_state_unknown_f()                 (0xd0U)
#define nvl_sl0_safe_ctrl2_tx_r()                                  (0x00002008U)
#define nvl_sl0_safe_ctrl2_tx_ctr_init_f(v)                 (((v)&0x7ffU) << 0U)
#define nvl_sl0_safe_ctrl2_tx_ctr_init_m()                   (U32(0x7ffU) << 0U)
#define nvl_sl0_safe_ctrl2_tx_ctr_init_v(r)               (((r) >> 0U) & 0x7ffU)
#define nvl_sl0_safe_ctrl2_tx_ctr_init_init_v()                    (0x00000728U)
#define nvl_sl0_safe_ctrl2_tx_ctr_init_init_f()                         (0x728U)
#define nvl_sl0_safe_ctrl2_tx_ctr_initscl_f(v)              (((v)&0x1fU) << 11U)
#define nvl_sl0_safe_ctrl2_tx_ctr_initscl_m()                (U32(0x1fU) << 11U)
#define nvl_sl0_safe_ctrl2_tx_ctr_initscl_v(r)            (((r) >> 11U) & 0x1fU)
#define nvl_sl0_safe_ctrl2_tx_ctr_initscl_init_v()                 (0x0000000fU)
#define nvl_sl0_safe_ctrl2_tx_ctr_initscl_init_f()                     (0x7800U)
#define nvl_sl1_error_rate_ctrl_r()                                (0x00003284U)
#define nvl_sl1_error_rate_ctrl_short_threshold_man_f(v)      (((v)&0x7U) << 0U)
#define nvl_sl1_error_rate_ctrl_short_threshold_man_m()        (U32(0x7U) << 0U)
#define nvl_sl1_error_rate_ctrl_short_threshold_man_v(r)    (((r) >> 0U) & 0x7U)
#define nvl_sl1_error_rate_ctrl_long_threshold_man_f(v)      (((v)&0x7U) << 16U)
#define nvl_sl1_error_rate_ctrl_long_threshold_man_m()        (U32(0x7U) << 16U)
#define nvl_sl1_error_rate_ctrl_long_threshold_man_v(r)    (((r) >> 16U) & 0x7U)
#define nvl_sl1_rxslsm_timeout_2_r()                               (0x00003034U)
#define nvl_txiobist_configreg_r()                                 (0x00002e14U)
#define nvl_txiobist_configreg_io_bist_mode_in_f(v)          (((v)&0x1U) << 17U)
#define nvl_txiobist_configreg_io_bist_mode_in_m()            (U32(0x1U) << 17U)
#define nvl_txiobist_configreg_io_bist_mode_in_v(r)        (((r) >> 17U) & 0x1U)
#define nvl_txiobist_config_r()                                    (0x00002e10U)
#define nvl_txiobist_config_dpg_prbsseedld_f(v)               (((v)&0x1U) << 2U)
#define nvl_txiobist_config_dpg_prbsseedld_m()                 (U32(0x1U) << 2U)
#define nvl_txiobist_config_dpg_prbsseedld_v(r)             (((r) >> 2U) & 0x1U)
#define nvl_intr_r()                                               (0x00000050U)
#define nvl_intr_tx_replay_f(v)                               (((v)&0x1U) << 0U)
#define nvl_intr_tx_replay_m()                                 (U32(0x1U) << 0U)
#define nvl_intr_tx_replay_v(r)                             (((r) >> 0U) & 0x1U)
#define nvl_intr_tx_recovery_short_f(v)                       (((v)&0x1U) << 1U)
#define nvl_intr_tx_recovery_short_m()                         (U32(0x1U) << 1U)
#define nvl_intr_tx_recovery_short_v(r)                     (((r) >> 1U) & 0x1U)
#define nvl_intr_tx_recovery_long_f(v)                        (((v)&0x1U) << 2U)
#define nvl_intr_tx_recovery_long_m()                          (U32(0x1U) << 2U)
#define nvl_intr_tx_recovery_long_v(r)                      (((r) >> 2U) & 0x1U)
#define nvl_intr_tx_fault_ram_f(v)                            (((v)&0x1U) << 4U)
#define nvl_intr_tx_fault_ram_m()                              (U32(0x1U) << 4U)
#define nvl_intr_tx_fault_ram_v(r)                          (((r) >> 4U) & 0x1U)
#define nvl_intr_tx_fault_interface_f(v)                      (((v)&0x1U) << 5U)
#define nvl_intr_tx_fault_interface_m()                        (U32(0x1U) << 5U)
#define nvl_intr_tx_fault_interface_v(r)                    (((r) >> 5U) & 0x1U)
#define nvl_intr_tx_fault_sublink_change_f(v)                 (((v)&0x1U) << 8U)
#define nvl_intr_tx_fault_sublink_change_m()                   (U32(0x1U) << 8U)
#define nvl_intr_tx_fault_sublink_change_v(r)               (((r) >> 8U) & 0x1U)
#define nvl_intr_rx_fault_sublink_change_f(v)                (((v)&0x1U) << 16U)
#define nvl_intr_rx_fault_sublink_change_m()                  (U32(0x1U) << 16U)
#define nvl_intr_rx_fault_sublink_change_v(r)              (((r) >> 16U) & 0x1U)
#define nvl_intr_rx_fault_dl_protocol_f(v)                   (((v)&0x1U) << 20U)
#define nvl_intr_rx_fault_dl_protocol_m()                     (U32(0x1U) << 20U)
#define nvl_intr_rx_fault_dl_protocol_v(r)                 (((r) >> 20U) & 0x1U)
#define nvl_intr_rx_short_error_rate_f(v)                    (((v)&0x1U) << 21U)
#define nvl_intr_rx_short_error_rate_m()                      (U32(0x1U) << 21U)
#define nvl_intr_rx_short_error_rate_v(r)                  (((r) >> 21U) & 0x1U)
#define nvl_intr_rx_long_error_rate_f(v)                     (((v)&0x1U) << 22U)
#define nvl_intr_rx_long_error_rate_m()                       (U32(0x1U) << 22U)
#define nvl_intr_rx_long_error_rate_v(r)                   (((r) >> 22U) & 0x1U)
#define nvl_intr_rx_ila_trigger_f(v)                         (((v)&0x1U) << 23U)
#define nvl_intr_rx_ila_trigger_m()                           (U32(0x1U) << 23U)
#define nvl_intr_rx_ila_trigger_v(r)                       (((r) >> 23U) & 0x1U)
#define nvl_intr_rx_crc_counter_f(v)                         (((v)&0x1U) << 24U)
#define nvl_intr_rx_crc_counter_m()                           (U32(0x1U) << 24U)
#define nvl_intr_rx_crc_counter_v(r)                       (((r) >> 24U) & 0x1U)
#define nvl_intr_ltssm_fault_f(v)                            (((v)&0x1U) << 28U)
#define nvl_intr_ltssm_fault_m()                              (U32(0x1U) << 28U)
#define nvl_intr_ltssm_fault_v(r)                          (((r) >> 28U) & 0x1U)
#define nvl_intr_ltssm_protocol_f(v)                         (((v)&0x1U) << 29U)
#define nvl_intr_ltssm_protocol_m()                           (U32(0x1U) << 29U)
#define nvl_intr_ltssm_protocol_v(r)                       (((r) >> 29U) & 0x1U)
#define nvl_intr_minion_request_f(v)                         (((v)&0x1U) << 30U)
#define nvl_intr_minion_request_m()                           (U32(0x1U) << 30U)
#define nvl_intr_minion_request_v(r)                       (((r) >> 30U) & 0x1U)
#define nvl_intr_sw2_r()                                           (0x00000054U)
#define nvl_intr_minion_r()                                        (0x00000060U)
#define nvl_intr_minion_tx_replay_f(v)                        (((v)&0x1U) << 0U)
#define nvl_intr_minion_tx_replay_m()                          (U32(0x1U) << 0U)
#define nvl_intr_minion_tx_replay_v(r)                      (((r) >> 0U) & 0x1U)
#define nvl_intr_minion_tx_recovery_short_f(v)                (((v)&0x1U) << 1U)
#define nvl_intr_minion_tx_recovery_short_m()                  (U32(0x1U) << 1U)
#define nvl_intr_minion_tx_recovery_short_v(r)              (((r) >> 1U) & 0x1U)
#define nvl_intr_minion_tx_recovery_long_f(v)                 (((v)&0x1U) << 2U)
#define nvl_intr_minion_tx_recovery_long_m()                   (U32(0x1U) << 2U)
#define nvl_intr_minion_tx_recovery_long_v(r)               (((r) >> 2U) & 0x1U)
#define nvl_intr_minion_tx_fault_ram_f(v)                     (((v)&0x1U) << 4U)
#define nvl_intr_minion_tx_fault_ram_m()                       (U32(0x1U) << 4U)
#define nvl_intr_minion_tx_fault_ram_v(r)                   (((r) >> 4U) & 0x1U)
#define nvl_intr_minion_tx_fault_interface_f(v)               (((v)&0x1U) << 5U)
#define nvl_intr_minion_tx_fault_interface_m()                 (U32(0x1U) << 5U)
#define nvl_intr_minion_tx_fault_interface_v(r)             (((r) >> 5U) & 0x1U)
#define nvl_intr_minion_tx_fault_sublink_change_f(v)          (((v)&0x1U) << 8U)
#define nvl_intr_minion_tx_fault_sublink_change_m()            (U32(0x1U) << 8U)
#define nvl_intr_minion_tx_fault_sublink_change_v(r)        (((r) >> 8U) & 0x1U)
#define nvl_intr_minion_rx_fault_sublink_change_f(v)         (((v)&0x1U) << 16U)
#define nvl_intr_minion_rx_fault_sublink_change_m()           (U32(0x1U) << 16U)
#define nvl_intr_minion_rx_fault_sublink_change_v(r)       (((r) >> 16U) & 0x1U)
#define nvl_intr_minion_rx_fault_dl_protocol_f(v)            (((v)&0x1U) << 20U)
#define nvl_intr_minion_rx_fault_dl_protocol_m()              (U32(0x1U) << 20U)
#define nvl_intr_minion_rx_fault_dl_protocol_v(r)          (((r) >> 20U) & 0x1U)
#define nvl_intr_minion_rx_short_error_rate_f(v)             (((v)&0x1U) << 21U)
#define nvl_intr_minion_rx_short_error_rate_m()               (U32(0x1U) << 21U)
#define nvl_intr_minion_rx_short_error_rate_v(r)           (((r) >> 21U) & 0x1U)
#define nvl_intr_minion_rx_long_error_rate_f(v)              (((v)&0x1U) << 22U)
#define nvl_intr_minion_rx_long_error_rate_m()                (U32(0x1U) << 22U)
#define nvl_intr_minion_rx_long_error_rate_v(r)            (((r) >> 22U) & 0x1U)
#define nvl_intr_minion_rx_ila_trigger_f(v)                  (((v)&0x1U) << 23U)
#define nvl_intr_minion_rx_ila_trigger_m()                    (U32(0x1U) << 23U)
#define nvl_intr_minion_rx_ila_trigger_v(r)                (((r) >> 23U) & 0x1U)
#define nvl_intr_minion_rx_crc_counter_f(v)                  (((v)&0x1U) << 24U)
#define nvl_intr_minion_rx_crc_counter_m()                    (U32(0x1U) << 24U)
#define nvl_intr_minion_rx_crc_counter_v(r)                (((r) >> 24U) & 0x1U)
#define nvl_intr_minion_ltssm_fault_f(v)                     (((v)&0x1U) << 28U)
#define nvl_intr_minion_ltssm_fault_m()                       (U32(0x1U) << 28U)
#define nvl_intr_minion_ltssm_fault_v(r)                   (((r) >> 28U) & 0x1U)
#define nvl_intr_minion_ltssm_protocol_f(v)                  (((v)&0x1U) << 29U)
#define nvl_intr_minion_ltssm_protocol_m()                    (U32(0x1U) << 29U)
#define nvl_intr_minion_ltssm_protocol_v(r)                (((r) >> 29U) & 0x1U)
#define nvl_intr_minion_minion_request_f(v)                  (((v)&0x1U) << 30U)
#define nvl_intr_minion_minion_request_m()                    (U32(0x1U) << 30U)
#define nvl_intr_minion_minion_request_v(r)                (((r) >> 30U) & 0x1U)
#define nvl_intr_nonstall_en_r()                                   (0x0000005cU)
#define nvl_intr_stall_en_r()                                      (0x00000058U)
#define nvl_intr_stall_en_tx_replay_f(v)                      (((v)&0x1U) << 0U)
#define nvl_intr_stall_en_tx_replay_m()                        (U32(0x1U) << 0U)
#define nvl_intr_stall_en_tx_replay_v(r)                    (((r) >> 0U) & 0x1U)
#define nvl_intr_stall_en_tx_recovery_short_f(v)              (((v)&0x1U) << 1U)
#define nvl_intr_stall_en_tx_recovery_short_m()                (U32(0x1U) << 1U)
#define nvl_intr_stall_en_tx_recovery_short_v(r)            (((r) >> 1U) & 0x1U)
#define nvl_intr_stall_en_tx_recovery_short_enable_v()             (0x00000001U)
#define nvl_intr_stall_en_tx_recovery_short_enable_f()                    (0x2U)
#define nvl_intr_stall_en_tx_recovery_long_f(v)               (((v)&0x1U) << 2U)
#define nvl_intr_stall_en_tx_recovery_long_m()                 (U32(0x1U) << 2U)
#define nvl_intr_stall_en_tx_recovery_long_v(r)             (((r) >> 2U) & 0x1U)
#define nvl_intr_stall_en_tx_recovery_long_enable_v()              (0x00000001U)
#define nvl_intr_stall_en_tx_recovery_long_enable_f()                     (0x4U)
#define nvl_intr_stall_en_tx_fault_ram_f(v)                   (((v)&0x1U) << 4U)
#define nvl_intr_stall_en_tx_fault_ram_m()                     (U32(0x1U) << 4U)
#define nvl_intr_stall_en_tx_fault_ram_v(r)                 (((r) >> 4U) & 0x1U)
#define nvl_intr_stall_en_tx_fault_ram_enable_v()                  (0x00000001U)
#define nvl_intr_stall_en_tx_fault_ram_enable_f()                        (0x10U)
#define nvl_intr_stall_en_tx_fault_interface_f(v)             (((v)&0x1U) << 5U)
#define nvl_intr_stall_en_tx_fault_interface_m()               (U32(0x1U) << 5U)
#define nvl_intr_stall_en_tx_fault_interface_v(r)           (((r) >> 5U) & 0x1U)
#define nvl_intr_stall_en_tx_fault_interface_enable_v()            (0x00000001U)
#define nvl_intr_stall_en_tx_fault_interface_enable_f()                  (0x20U)
#define nvl_intr_stall_en_tx_fault_sublink_change_f(v)        (((v)&0x1U) << 8U)
#define nvl_intr_stall_en_tx_fault_sublink_change_m()          (U32(0x1U) << 8U)
#define nvl_intr_stall_en_tx_fault_sublink_change_v(r)      (((r) >> 8U) & 0x1U)
#define nvl_intr_stall_en_tx_fault_sublink_change_enable_v()       (0x00000001U)
#define nvl_intr_stall_en_tx_fault_sublink_change_enable_f()            (0x100U)
#define nvl_intr_stall_en_rx_fault_sublink_change_f(v)       (((v)&0x1U) << 16U)
#define nvl_intr_stall_en_rx_fault_sublink_change_m()         (U32(0x1U) << 16U)
#define nvl_intr_stall_en_rx_fault_sublink_change_v(r)     (((r) >> 16U) & 0x1U)
#define nvl_intr_stall_en_rx_fault_sublink_change_enable_v()       (0x00000001U)
#define nvl_intr_stall_en_rx_fault_sublink_change_enable_f()          (0x10000U)
#define nvl_intr_stall_en_rx_fault_dl_protocol_f(v)          (((v)&0x1U) << 20U)
#define nvl_intr_stall_en_rx_fault_dl_protocol_m()            (U32(0x1U) << 20U)
#define nvl_intr_stall_en_rx_fault_dl_protocol_v(r)        (((r) >> 20U) & 0x1U)
#define nvl_intr_stall_en_rx_fault_dl_protocol_enable_v()          (0x00000001U)
#define nvl_intr_stall_en_rx_fault_dl_protocol_enable_f()            (0x100000U)
#define nvl_intr_stall_en_rx_short_error_rate_f(v)           (((v)&0x1U) << 21U)
#define nvl_intr_stall_en_rx_short_error_rate_m()             (U32(0x1U) << 21U)
#define nvl_intr_stall_en_rx_short_error_rate_v(r)         (((r) >> 21U) & 0x1U)
#define nvl_intr_stall_en_rx_short_error_rate_enable_v()           (0x00000001U)
#define nvl_intr_stall_en_rx_short_error_rate_enable_f()             (0x200000U)
#define nvl_intr_stall_en_rx_long_error_rate_f(v)            (((v)&0x1U) << 22U)
#define nvl_intr_stall_en_rx_long_error_rate_m()              (U32(0x1U) << 22U)
#define nvl_intr_stall_en_rx_long_error_rate_v(r)          (((r) >> 22U) & 0x1U)
#define nvl_intr_stall_en_rx_long_error_rate_enable_v()            (0x00000001U)
#define nvl_intr_stall_en_rx_long_error_rate_enable_f()              (0x400000U)
#define nvl_intr_stall_en_rx_ila_trigger_f(v)                (((v)&0x1U) << 23U)
#define nvl_intr_stall_en_rx_ila_trigger_m()                  (U32(0x1U) << 23U)
#define nvl_intr_stall_en_rx_ila_trigger_v(r)              (((r) >> 23U) & 0x1U)
#define nvl_intr_stall_en_rx_ila_trigger_enable_v()                (0x00000001U)
#define nvl_intr_stall_en_rx_ila_trigger_enable_f()                  (0x800000U)
#define nvl_intr_stall_en_rx_crc_counter_f(v)                (((v)&0x1U) << 24U)
#define nvl_intr_stall_en_rx_crc_counter_m()                  (U32(0x1U) << 24U)
#define nvl_intr_stall_en_rx_crc_counter_v(r)              (((r) >> 24U) & 0x1U)
#define nvl_intr_stall_en_rx_crc_counter_enable_v()                (0x00000001U)
#define nvl_intr_stall_en_rx_crc_counter_enable_f()                 (0x1000000U)
#define nvl_intr_stall_en_ltssm_fault_f(v)                   (((v)&0x1U) << 28U)
#define nvl_intr_stall_en_ltssm_fault_m()                     (U32(0x1U) << 28U)
#define nvl_intr_stall_en_ltssm_fault_v(r)                 (((r) >> 28U) & 0x1U)
#define nvl_intr_stall_en_ltssm_fault_enable_v()                   (0x00000001U)
#define nvl_intr_stall_en_ltssm_fault_enable_f()                   (0x10000000U)
#define nvl_intr_stall_en_ltssm_protocol_f(v)                (((v)&0x1U) << 29U)
#define nvl_intr_stall_en_ltssm_protocol_m()                  (U32(0x1U) << 29U)
#define nvl_intr_stall_en_ltssm_protocol_v(r)              (((r) >> 29U) & 0x1U)
#define nvl_intr_stall_en_ltssm_protocol_enable_v()                (0x00000001U)
#define nvl_intr_stall_en_ltssm_protocol_enable_f()                (0x20000000U)
#define nvl_intr_stall_en_minion_request_f(v)                (((v)&0x1U) << 30U)
#define nvl_intr_stall_en_minion_request_m()                  (U32(0x1U) << 30U)
#define nvl_intr_stall_en_minion_request_v(r)              (((r) >> 30U) & 0x1U)
#define nvl_intr_stall_en_minion_request_enable_v()                (0x00000001U)
#define nvl_intr_stall_en_minion_request_enable_f()                (0x40000000U)
#define nvl_br0_cfg_cal_r()                                        (0x0000281cU)
#define nvl_br0_cfg_cal_rxcal_f(v)                            (((v)&0x1U) << 0U)
#define nvl_br0_cfg_cal_rxcal_m()                              (U32(0x1U) << 0U)
#define nvl_br0_cfg_cal_rxcal_v(r)                          (((r) >> 0U) & 0x1U)
#define nvl_br0_cfg_cal_rxcal_on_v()                               (0x00000001U)
#define nvl_br0_cfg_cal_rxcal_on_f()                                      (0x1U)
#define nvl_br0_cfg_status_cal_r()                                 (0x00002838U)
#define nvl_br0_cfg_status_cal_rxcal_done_f(v)                (((v)&0x1U) << 2U)
#define nvl_br0_cfg_status_cal_rxcal_done_m()                  (U32(0x1U) << 2U)
#define nvl_br0_cfg_status_cal_rxcal_done_v(r)              (((r) >> 2U) & 0x1U)
#define nvl_sl0_link_rxdet_status_r()                              (0x00002228U)
#define nvl_sl0_link_rxdet_status_sts_v(r)                 (((r) >> 28U) & 0x3U)
#define nvl_sl0_link_rxdet_status_sts_found_v()                    (0x00000002U)
#define nvl_sl0_link_rxdet_status_sts_timeout_v()                  (0x00000003U)
#define nvl_clk_status_r()                                         (0x0000001cU)
#define nvl_clk_status_txclk_sts_v(r)                      (((r) >> 19U) & 0x1U)
#define nvl_clk_status_txclk_sts_pll_clk_v()                       (0x00000000U)
#endif
