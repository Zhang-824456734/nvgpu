/*
 * GP10B GPU GR
 *
 * Copyright (c) 2015-2019, NVIDIA CORPORATION.  All rights reserved.
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

#include <nvgpu/timers.h>
#include <nvgpu/kmem.h>
#include <nvgpu/gmmu.h>
#include <nvgpu/dma.h>
#include <nvgpu/bug.h>
#include <nvgpu/debug.h>
#include <nvgpu/debugger.h>
#include <nvgpu/fuse.h>
#include <nvgpu/enabled.h>
#include <nvgpu/io.h>
#include <nvgpu/utils.h>
#include <nvgpu/gk20a.h>
#include <nvgpu/channel.h>
#include <nvgpu/regops.h>
#include <nvgpu/gr/subctx.h>
#include <nvgpu/gr/ctx.h>
#include <nvgpu/gr/config.h>
#include <nvgpu/engines.h>
#include <nvgpu/engine_status.h>

#include "gk20a/gr_gk20a.h"

#include "gm20b/gr_gm20b.h"
#include "gp10b/gr_gp10b.h"

#include <nvgpu/hw/gp10b/hw_gr_gp10b.h>
#include <nvgpu/hw/gp10b/hw_fifo_gp10b.h>

#define GFXP_WFI_TIMEOUT_COUNT_DEFAULT 100000U

bool gr_gp10b_is_valid_class(struct gk20a *g, u32 class_num)
{
	bool valid = false;

	nvgpu_speculation_barrier();
	switch (class_num) {
	case PASCAL_COMPUTE_A:
	case PASCAL_A:
	case PASCAL_DMA_COPY_A:
		valid = true;
		break;

	case MAXWELL_COMPUTE_B:
	case MAXWELL_B:
	case FERMI_TWOD_A:
	case KEPLER_DMA_COPY_A:
	case MAXWELL_DMA_COPY_A:
		valid = true;
		break;

	default:
		break;
	}
	nvgpu_log_info(g, "class=0x%x valid=%d", class_num, valid);
	return valid;
}

bool gr_gp10b_is_valid_gfx_class(struct gk20a *g, u32 class_num)
{
	if (class_num == PASCAL_A ||  class_num == MAXWELL_B) {
		return true;
	} else {
		return false;
	}
}

bool gr_gp10b_is_valid_compute_class(struct gk20a *g, u32 class_num)
{
	if (class_num == PASCAL_COMPUTE_A ||  class_num == MAXWELL_COMPUTE_B) {
		return true;
	} else {
		return false;
	}
}


static void gr_gp10b_sm_lrf_ecc_overcount_war(bool single_err,
						u32 sed_status,
						u32 ded_status,
						u32 *count_to_adjust,
						u32 opposite_count)
{
	u32 over_count = 0;

	sed_status >>= gr_pri_gpc0_tpc0_sm_lrf_ecc_status_single_err_detected_qrfdp0_b();
	ded_status >>= gr_pri_gpc0_tpc0_sm_lrf_ecc_status_double_err_detected_qrfdp0_b();

	/* One overcount for each partition on which a SBE occurred but not a
	   DBE (or vice-versa) */
	if (single_err) {
		over_count = (u32)hweight32(sed_status & ~ded_status);
	} else {
		over_count = (u32)hweight32(ded_status & ~sed_status);
	}

	/* If both a SBE and a DBE occur on the same partition, then we have an
	   overcount for the subpartition if the opposite error counts are
	   zero. */
	if (((sed_status & ded_status) != 0U) && (opposite_count == 0U)) {
		over_count += (u32)hweight32(sed_status & ded_status);
	}

	if (*count_to_adjust > over_count) {
		*count_to_adjust -= over_count;
	} else {
		*count_to_adjust = 0;
	}
}

int gr_gp10b_handle_sm_exception(struct gk20a *g,
			u32 gpc, u32 tpc, u32 sm,
			bool *post_event, struct channel_gk20a *fault_ch,
			u32 *hww_global_esr)
{
	int ret = 0;
	u32 gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_GPC_STRIDE);
	u32 tpc_in_gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_TPC_IN_GPC_STRIDE);
	u32 offset = gpc_stride * gpc + tpc_in_gpc_stride * tpc;
	u32 lrf_ecc_status, lrf_ecc_sed_status, lrf_ecc_ded_status;
	u32 lrf_single_count_delta, lrf_double_count_delta;
	u32 shm_ecc_status;

	ret = gr_gk20a_handle_sm_exception(g,
		gpc, tpc, sm, post_event, fault_ch, hww_global_esr);

	/* Check for LRF ECC errors. */
        lrf_ecc_status = gk20a_readl(g,
			gr_pri_gpc0_tpc0_sm_lrf_ecc_status_r() + offset);
	lrf_ecc_sed_status = lrf_ecc_status &
				(gr_pri_gpc0_tpc0_sm_lrf_ecc_status_single_err_detected_qrfdp0_pending_f() |
				 gr_pri_gpc0_tpc0_sm_lrf_ecc_status_single_err_detected_qrfdp1_pending_f() |
				 gr_pri_gpc0_tpc0_sm_lrf_ecc_status_single_err_detected_qrfdp2_pending_f() |
				 gr_pri_gpc0_tpc0_sm_lrf_ecc_status_single_err_detected_qrfdp3_pending_f());
	lrf_ecc_ded_status = lrf_ecc_status &
				(gr_pri_gpc0_tpc0_sm_lrf_ecc_status_double_err_detected_qrfdp0_pending_f() |
				 gr_pri_gpc0_tpc0_sm_lrf_ecc_status_double_err_detected_qrfdp1_pending_f() |
				 gr_pri_gpc0_tpc0_sm_lrf_ecc_status_double_err_detected_qrfdp2_pending_f() |
				 gr_pri_gpc0_tpc0_sm_lrf_ecc_status_double_err_detected_qrfdp3_pending_f());
	lrf_single_count_delta =
		gk20a_readl(g,
			gr_pri_gpc0_tpc0_sm_lrf_ecc_single_err_count_r() +
			offset);
	lrf_double_count_delta =
		gk20a_readl(g,
			gr_pri_gpc0_tpc0_sm_lrf_ecc_double_err_count_r() +
			offset);
	gk20a_writel(g,
		gr_pri_gpc0_tpc0_sm_lrf_ecc_single_err_count_r() + offset,
		0);
	gk20a_writel(g,
		gr_pri_gpc0_tpc0_sm_lrf_ecc_double_err_count_r() + offset,
		0);
	if (lrf_ecc_sed_status != 0U) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_intr,
			"Single bit error detected in SM LRF!");

		gr_gp10b_sm_lrf_ecc_overcount_war(true,
						lrf_ecc_sed_status,
						lrf_ecc_ded_status,
						&lrf_single_count_delta,
						lrf_double_count_delta);
		g->ecc.gr.sm_lrf_ecc_single_err_count[gpc][tpc].counter +=
							lrf_single_count_delta;
	}
	if (lrf_ecc_ded_status != 0U) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_intr,
			"Double bit error detected in SM LRF!");

		gr_gp10b_sm_lrf_ecc_overcount_war(false,
						lrf_ecc_sed_status,
						lrf_ecc_ded_status,
						&lrf_double_count_delta,
						lrf_single_count_delta);
		g->ecc.gr.sm_lrf_ecc_double_err_count[gpc][tpc].counter +=
							lrf_double_count_delta;
	}
	gk20a_writel(g, gr_pri_gpc0_tpc0_sm_lrf_ecc_status_r() + offset,
			lrf_ecc_status);

	/* Check for SHM ECC errors. */
        shm_ecc_status = gk20a_readl(g,
			gr_pri_gpc0_tpc0_sm_shm_ecc_status_r() + offset);
	if ((shm_ecc_status &
		gr_pri_gpc0_tpc0_sm_shm_ecc_status_single_err_corrected_shm0_pending_f()) != 0U ||
		(shm_ecc_status &
		gr_pri_gpc0_tpc0_sm_shm_ecc_status_single_err_corrected_shm1_pending_f()) != 0U ||
		(shm_ecc_status &
		gr_pri_gpc0_tpc0_sm_shm_ecc_status_single_err_detected_shm0_pending_f()) != 0U ||
		(shm_ecc_status &
		gr_pri_gpc0_tpc0_sm_shm_ecc_status_single_err_detected_shm1_pending_f()) != 0U ) {
		u32 ecc_stats_reg_val;

		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_intr,
			"Single bit error detected in SM SHM!");

		ecc_stats_reg_val =
			gk20a_readl(g,
				gr_pri_gpc0_tpc0_sm_shm_ecc_err_count_r() + offset);
		g->ecc.gr.sm_shm_ecc_sec_count[gpc][tpc].counter +=
			gr_pri_gpc0_tpc0_sm_shm_ecc_err_count_single_corrected_v(ecc_stats_reg_val);
		g->ecc.gr.sm_shm_ecc_sed_count[gpc][tpc].counter +=
			gr_pri_gpc0_tpc0_sm_shm_ecc_err_count_single_detected_v(ecc_stats_reg_val);
		ecc_stats_reg_val &= ~(gr_pri_gpc0_tpc0_sm_shm_ecc_err_count_single_corrected_m() |
					gr_pri_gpc0_tpc0_sm_shm_ecc_err_count_single_detected_m());
		gk20a_writel(g,
			gr_pri_gpc0_tpc0_sm_shm_ecc_err_count_r() + offset,
			ecc_stats_reg_val);
	}
	if ((shm_ecc_status &
		gr_pri_gpc0_tpc0_sm_shm_ecc_status_double_err_detected_shm0_pending_f()) != 0U ||
		(shm_ecc_status &
		gr_pri_gpc0_tpc0_sm_shm_ecc_status_double_err_detected_shm1_pending_f()) != 0U) {
		u32 ecc_stats_reg_val;

		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_intr,
			"Double bit error detected in SM SHM!");

		ecc_stats_reg_val =
			gk20a_readl(g,
				gr_pri_gpc0_tpc0_sm_shm_ecc_err_count_r() + offset);
		g->ecc.gr.sm_shm_ecc_ded_count[gpc][tpc].counter +=
			gr_pri_gpc0_tpc0_sm_shm_ecc_err_count_double_detected_v(ecc_stats_reg_val);
		ecc_stats_reg_val &= ~(gr_pri_gpc0_tpc0_sm_shm_ecc_err_count_double_detected_m());
		gk20a_writel(g,
			gr_pri_gpc0_tpc0_sm_shm_ecc_err_count_r() + offset,
			ecc_stats_reg_val);
	}
	gk20a_writel(g, gr_pri_gpc0_tpc0_sm_shm_ecc_status_r() + offset,
			shm_ecc_status);


	return ret;
}

int gr_gp10b_handle_tex_exception(struct gk20a *g, u32 gpc, u32 tpc,
		bool *post_event)
{
	int ret = 0;
	u32 gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_GPC_STRIDE);
	u32 tpc_in_gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_TPC_IN_GPC_STRIDE);
	u32 offset = gpc_stride * gpc + tpc_in_gpc_stride * tpc;
	u32 esr;
	u32 ecc_stats_reg_val;

	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg, " ");

	esr = gk20a_readl(g,
			 gr_gpc0_tpc0_tex_m_hww_esr_r() + offset);
	nvgpu_log(g, gpu_dbg_intr | gpu_dbg_gpu_dbg, "0x%08x", esr);

	if ((esr & gr_gpc0_tpc0_tex_m_hww_esr_ecc_sec_pending_f()) != 0U) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_intr,
			"Single bit error detected in TEX!");

		/* Pipe 0 counters */
		gk20a_writel(g,
			gr_pri_gpc0_tpc0_tex_m_routing_r() + offset,
			gr_pri_gpc0_tpc0_tex_m_routing_sel_pipe0_f());

		ecc_stats_reg_val = gk20a_readl(g,
				gr_pri_gpc0_tpc0_tex_m_ecc_cnt_total_r() + offset);
		g->ecc.gr.tex_ecc_total_sec_pipe0_count[gpc][tpc].counter +=
				gr_pri_gpc0_tpc0_tex_m_ecc_cnt_total_sec_v(ecc_stats_reg_val);
		ecc_stats_reg_val &= ~gr_pri_gpc0_tpc0_tex_m_ecc_cnt_total_sec_m();
		gk20a_writel(g,
			gr_pri_gpc0_tpc0_tex_m_ecc_cnt_total_r() + offset,
			ecc_stats_reg_val);

		ecc_stats_reg_val = gk20a_readl(g,
				gr_pri_gpc0_tpc0_tex_m_ecc_cnt_unique_r() + offset);
		g->ecc.gr.tex_unique_ecc_sec_pipe0_count[gpc][tpc].counter +=
				gr_pri_gpc0_tpc0_tex_m_ecc_cnt_unique_sec_v(ecc_stats_reg_val);
		ecc_stats_reg_val &= ~gr_pri_gpc0_tpc0_tex_m_ecc_cnt_unique_sec_m();
		gk20a_writel(g,
			gr_pri_gpc0_tpc0_tex_m_ecc_cnt_unique_r() + offset,
			ecc_stats_reg_val);


		/* Pipe 1 counters */
		gk20a_writel(g,
			gr_pri_gpc0_tpc0_tex_m_routing_r() + offset,
			gr_pri_gpc0_tpc0_tex_m_routing_sel_pipe1_f());

		ecc_stats_reg_val = gk20a_readl(g,
				gr_pri_gpc0_tpc0_tex_m_ecc_cnt_total_r() + offset);
		g->ecc.gr.tex_ecc_total_sec_pipe1_count[gpc][tpc].counter +=
				gr_pri_gpc0_tpc0_tex_m_ecc_cnt_total_sec_v(ecc_stats_reg_val);
		ecc_stats_reg_val &= ~gr_pri_gpc0_tpc0_tex_m_ecc_cnt_total_sec_m();
		gk20a_writel(g,
			gr_pri_gpc0_tpc0_tex_m_ecc_cnt_total_r() + offset,
			ecc_stats_reg_val);

		ecc_stats_reg_val = gk20a_readl(g,
				gr_pri_gpc0_tpc0_tex_m_ecc_cnt_unique_r() + offset);
		g->ecc.gr.tex_unique_ecc_sec_pipe1_count[gpc][tpc].counter +=
				gr_pri_gpc0_tpc0_tex_m_ecc_cnt_unique_sec_v(ecc_stats_reg_val);
		ecc_stats_reg_val &= ~gr_pri_gpc0_tpc0_tex_m_ecc_cnt_unique_sec_m();
		gk20a_writel(g,
			gr_pri_gpc0_tpc0_tex_m_ecc_cnt_unique_r() + offset,
			ecc_stats_reg_val);


		gk20a_writel(g,
			gr_pri_gpc0_tpc0_tex_m_routing_r() + offset,
			gr_pri_gpc0_tpc0_tex_m_routing_sel_default_f());
	}
	if ((esr & gr_gpc0_tpc0_tex_m_hww_esr_ecc_ded_pending_f()) != 0U) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_intr,
			"Double bit error detected in TEX!");

		/* Pipe 0 counters */
		gk20a_writel(g,
			gr_pri_gpc0_tpc0_tex_m_routing_r() + offset,
			gr_pri_gpc0_tpc0_tex_m_routing_sel_pipe0_f());

		ecc_stats_reg_val = gk20a_readl(g,
				gr_pri_gpc0_tpc0_tex_m_ecc_cnt_total_r() + offset);
		g->ecc.gr.tex_ecc_total_ded_pipe0_count[gpc][tpc].counter +=
				gr_pri_gpc0_tpc0_tex_m_ecc_cnt_total_ded_v(ecc_stats_reg_val);
		ecc_stats_reg_val &= ~gr_pri_gpc0_tpc0_tex_m_ecc_cnt_total_ded_m();
		gk20a_writel(g,
			gr_pri_gpc0_tpc0_tex_m_ecc_cnt_total_r() + offset,
			ecc_stats_reg_val);

		ecc_stats_reg_val = gk20a_readl(g,
				gr_pri_gpc0_tpc0_tex_m_ecc_cnt_unique_r() + offset);
		g->ecc.gr.tex_unique_ecc_ded_pipe0_count[gpc][tpc].counter +=
				gr_pri_gpc0_tpc0_tex_m_ecc_cnt_unique_ded_v(ecc_stats_reg_val);
		ecc_stats_reg_val &= ~gr_pri_gpc0_tpc0_tex_m_ecc_cnt_unique_ded_m();
		gk20a_writel(g,
			gr_pri_gpc0_tpc0_tex_m_ecc_cnt_unique_r() + offset,
			ecc_stats_reg_val);


		/* Pipe 1 counters */
		gk20a_writel(g,
			gr_pri_gpc0_tpc0_tex_m_routing_r() + offset,
			gr_pri_gpc0_tpc0_tex_m_routing_sel_pipe1_f());

		ecc_stats_reg_val = gk20a_readl(g,
				gr_pri_gpc0_tpc0_tex_m_ecc_cnt_total_r() + offset);
		g->ecc.gr.tex_ecc_total_ded_pipe1_count[gpc][tpc].counter +=
				gr_pri_gpc0_tpc0_tex_m_ecc_cnt_total_ded_v(ecc_stats_reg_val);
		ecc_stats_reg_val &= ~gr_pri_gpc0_tpc0_tex_m_ecc_cnt_total_ded_m();
		gk20a_writel(g,
			gr_pri_gpc0_tpc0_tex_m_ecc_cnt_total_r() + offset,
			ecc_stats_reg_val);

		ecc_stats_reg_val = gk20a_readl(g,
				gr_pri_gpc0_tpc0_tex_m_ecc_cnt_unique_r() + offset);
		g->ecc.gr.tex_unique_ecc_ded_pipe1_count[gpc][tpc].counter +=
				gr_pri_gpc0_tpc0_tex_m_ecc_cnt_unique_ded_v(ecc_stats_reg_val);
		ecc_stats_reg_val &= ~gr_pri_gpc0_tpc0_tex_m_ecc_cnt_unique_ded_m();
		gk20a_writel(g,
			gr_pri_gpc0_tpc0_tex_m_ecc_cnt_unique_r() + offset,
			ecc_stats_reg_val);


		gk20a_writel(g,
			gr_pri_gpc0_tpc0_tex_m_routing_r() + offset,
			gr_pri_gpc0_tpc0_tex_m_routing_sel_default_f());
	}

	gk20a_writel(g,
		     gr_gpc0_tpc0_tex_m_hww_esr_r() + offset,
		     esr | gr_gpc0_tpc0_tex_m_hww_esr_reset_active_f());

	return ret;
}

int gr_gp10b_commit_global_cb_manager(struct gk20a *g,
			struct nvgpu_gr_ctx *gr_ctx, bool patch)
{
	struct gr_gk20a *gr = &g->gr;
	u32 attrib_offset_in_chunk = 0;
	u32 alpha_offset_in_chunk = 0;
	u32 pd_ab_max_output;
	u32 gpc_index, ppc_index;
	u32 temp, temp2;
	u32 cbm_cfg_size_beta, cbm_cfg_size_alpha, cbm_cfg_size_steadystate;
	u32 attrib_size_in_chunk, cb_attrib_cache_size_init;
	u32 gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_GPC_STRIDE);
	u32 ppc_in_gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_PPC_IN_GPC_STRIDE);
	u32 num_pes_per_gpc = nvgpu_get_litter_value(g, GPU_LIT_NUM_PES_PER_GPC);

	nvgpu_log_fn(g, " ");

	if (gr_ctx->graphics_preempt_mode == NVGPU_PREEMPTION_MODE_GRAPHICS_GFXP) {
		attrib_size_in_chunk = gr->attrib_cb_gfxp_size;
		cb_attrib_cache_size_init = gr->attrib_cb_gfxp_default_size;
	} else {
		attrib_size_in_chunk = gr->attrib_cb_size;
		cb_attrib_cache_size_init = gr->attrib_cb_default_size;
	}

	nvgpu_gr_ctx_patch_write(g, gr_ctx, gr_ds_tga_constraintlogic_beta_r(),
		gr->attrib_cb_default_size, patch);
	nvgpu_gr_ctx_patch_write(g, gr_ctx, gr_ds_tga_constraintlogic_alpha_r(),
		gr->alpha_cb_default_size, patch);

	pd_ab_max_output = (gr->alpha_cb_default_size *
		gr_gpc0_ppc0_cbm_beta_cb_size_v_granularity_v()) /
		gr_pd_ab_dist_cfg1_max_output_granularity_v();

	if (g->gr.pd_max_batches != 0U) {
		nvgpu_gr_ctx_patch_write(g, gr_ctx, gr_pd_ab_dist_cfg1_r(),
			gr_pd_ab_dist_cfg1_max_output_f(pd_ab_max_output) |
			gr_pd_ab_dist_cfg1_max_batches_f(g->gr.pd_max_batches), patch);
	} else {
		nvgpu_gr_ctx_patch_write(g, gr_ctx, gr_pd_ab_dist_cfg1_r(),
			gr_pd_ab_dist_cfg1_max_output_f(pd_ab_max_output) |
			gr_pd_ab_dist_cfg1_max_batches_init_f(), patch);
	}

	attrib_offset_in_chunk = alpha_offset_in_chunk +
		nvgpu_gr_config_get_tpc_count(gr->config) * gr->alpha_cb_size;

	for (gpc_index = 0;
	     gpc_index < nvgpu_gr_config_get_gpc_count(gr->config);
	     gpc_index++) {
		temp = gpc_stride * gpc_index;
		temp2 = num_pes_per_gpc * gpc_index;
		for (ppc_index = 0;
		     ppc_index < nvgpu_gr_config_get_gpc_ppc_count(gr->config, gpc_index);
		     ppc_index++) {
			cbm_cfg_size_beta = cb_attrib_cache_size_init *
				nvgpu_gr_config_get_pes_tpc_count(gr->config,
					gpc_index, ppc_index);
			cbm_cfg_size_alpha = gr->alpha_cb_default_size *
				nvgpu_gr_config_get_pes_tpc_count(gr->config,
					gpc_index, ppc_index);
			cbm_cfg_size_steadystate = gr->attrib_cb_default_size *
				nvgpu_gr_config_get_pes_tpc_count(gr->config,
					gpc_index, ppc_index);

			nvgpu_gr_ctx_patch_write(g, gr_ctx,
				gr_gpc0_ppc0_cbm_beta_cb_size_r() + temp +
				ppc_in_gpc_stride * ppc_index,
				cbm_cfg_size_beta, patch);

			nvgpu_gr_ctx_patch_write(g, gr_ctx,
				gr_gpc0_ppc0_cbm_beta_cb_offset_r() + temp +
				ppc_in_gpc_stride * ppc_index,
				attrib_offset_in_chunk, patch);

			nvgpu_gr_ctx_patch_write(g, gr_ctx,
				gr_gpc0_ppc0_cbm_beta_steady_state_cb_size_r() + temp +
				ppc_in_gpc_stride * ppc_index,
				cbm_cfg_size_steadystate,
				patch);

			attrib_offset_in_chunk += attrib_size_in_chunk *
				nvgpu_gr_config_get_pes_tpc_count(gr->config,
					gpc_index, ppc_index);

			nvgpu_gr_ctx_patch_write(g, gr_ctx,
				gr_gpc0_ppc0_cbm_alpha_cb_size_r() + temp +
				ppc_in_gpc_stride * ppc_index,
				cbm_cfg_size_alpha, patch);

			nvgpu_gr_ctx_patch_write(g, gr_ctx,
				gr_gpc0_ppc0_cbm_alpha_cb_offset_r() + temp +
				ppc_in_gpc_stride * ppc_index,
				alpha_offset_in_chunk, patch);

			alpha_offset_in_chunk += gr->alpha_cb_size *
				nvgpu_gr_config_get_pes_tpc_count(gr->config,
					gpc_index, ppc_index);

			nvgpu_gr_ctx_patch_write(g, gr_ctx,
				gr_gpcs_swdx_tc_beta_cb_size_r(ppc_index + temp2),
				gr_gpcs_swdx_tc_beta_cb_size_v_f(cbm_cfg_size_steadystate),
				patch);
		}
	}

	return 0;
}

void gr_gp10b_commit_global_pagepool(struct gk20a *g,
					    struct nvgpu_gr_ctx *gr_ctx,
					    u64 addr, u32 size, bool patch)
{
	nvgpu_assert(u64_hi32(addr) == 0U);

	nvgpu_gr_ctx_patch_write(g, gr_ctx, gr_scc_pagepool_base_r(),
		gr_scc_pagepool_base_addr_39_8_f((u32)addr), patch);

	nvgpu_gr_ctx_patch_write(g, gr_ctx, gr_scc_pagepool_r(),
		gr_scc_pagepool_total_pages_f(size) |
		gr_scc_pagepool_valid_true_f(), patch);

	nvgpu_gr_ctx_patch_write(g, gr_ctx, gr_gpcs_gcc_pagepool_base_r(),
		gr_gpcs_gcc_pagepool_base_addr_39_8_f((u32)addr), patch);

	nvgpu_gr_ctx_patch_write(g, gr_ctx, gr_gpcs_gcc_pagepool_r(),
		gr_gpcs_gcc_pagepool_total_pages_f(size), patch);
}

u32 gr_gp10b_pagepool_default_size(struct gk20a *g)
{
	return gr_scc_pagepool_total_pages_hwmax_value_v();
}

u32 gr_gp10b_calc_global_ctx_buffer_size(struct gk20a *g)
{
	struct gr_gk20a *gr = &g->gr;
	u32 size;

	gr->attrib_cb_size = gr->attrib_cb_default_size;
	gr->alpha_cb_size = gr->alpha_cb_default_size;

	gr->attrib_cb_size = min(gr->attrib_cb_size,
		 gr_gpc0_ppc0_cbm_beta_cb_size_v_f(~U32(0U)) /
			nvgpu_gr_config_get_tpc_count(gr->config));
	gr->alpha_cb_size = min(gr->alpha_cb_size,
		 gr_gpc0_ppc0_cbm_alpha_cb_size_v_f(~U32(0U)) /
			nvgpu_gr_config_get_tpc_count(gr->config));

	size = gr->attrib_cb_size *
		gr_gpc0_ppc0_cbm_beta_cb_size_v_granularity_v() *
		nvgpu_gr_config_get_max_tpc_count(gr->config);

	size += gr->alpha_cb_size *
		gr_gpc0_ppc0_cbm_alpha_cb_size_v_granularity_v() *
		nvgpu_gr_config_get_max_tpc_count(gr->config);

	size = ALIGN(size, 128);

	return size;
}

static void gr_gp10b_set_go_idle_timeout(struct gk20a *g, u32 data)
{
	gk20a_writel(g, gr_fe_go_idle_timeout_r(), data);
}

static void gr_gp10b_set_coalesce_buffer_size(struct gk20a *g, u32 data)
{
	u32 val;

	nvgpu_log_fn(g, " ");

	val = gk20a_readl(g, gr_gpcs_tc_debug0_r());
	val = set_field(val, gr_gpcs_tc_debug0_limit_coalesce_buffer_size_m(),
			     gr_gpcs_tc_debug0_limit_coalesce_buffer_size_f(data));
	gk20a_writel(g, gr_gpcs_tc_debug0_r(), val);

	nvgpu_log_fn(g, "done");
}

void gr_gp10b_set_bes_crop_debug3(struct gk20a *g, u32 data)
{
	u32 val;

	nvgpu_log_fn(g, " ");

	val = gk20a_readl(g, gr_bes_crop_debug3_r());
	if ((data & 1U) != 0U) {
		val = set_field(val,
				gr_bes_crop_debug3_blendopt_read_suppress_m(),
				gr_bes_crop_debug3_blendopt_read_suppress_enabled_f());
		val = set_field(val,
				gr_bes_crop_debug3_blendopt_fill_override_m(),
				gr_bes_crop_debug3_blendopt_fill_override_enabled_f());
	} else {
		val = set_field(val,
				gr_bes_crop_debug3_blendopt_read_suppress_m(),
				gr_bes_crop_debug3_blendopt_read_suppress_disabled_f());
		val = set_field(val,
				gr_bes_crop_debug3_blendopt_fill_override_m(),
				gr_bes_crop_debug3_blendopt_fill_override_disabled_f());
	}
	gk20a_writel(g, gr_bes_crop_debug3_r(), val);
}

void gr_gp10b_set_bes_crop_debug4(struct gk20a *g, u32 data)
{
	u32 val;

	nvgpu_log_fn(g, " ");

	val = gk20a_readl(g, gr_bes_crop_debug4_r());
	if ((data & NVC097_BES_CROP_DEBUG4_CLAMP_FP_BLEND_TO_MAXVAL) != 0U) {
		val = set_field(val,
			gr_bes_crop_debug4_clamp_fp_blend_m(),
			gr_bes_crop_debug4_clamp_fp_blend_to_maxval_f());
	} else if ((data & NVC097_BES_CROP_DEBUG4_CLAMP_FP_BLEND_TO_INF) != 0U) {
		val = set_field(val,
			gr_bes_crop_debug4_clamp_fp_blend_m(),
			gr_bes_crop_debug4_clamp_fp_blend_to_inf_f());
	} else {
		nvgpu_warn(g,
			"gr_gp10b_set_bes_crop_debug4: wrong data sent!");
		return;
	}
	gk20a_writel(g, gr_bes_crop_debug4_r(), val);
}


int gr_gp10b_handle_sw_method(struct gk20a *g, u32 addr,
				     u32 class_num, u32 offset, u32 data)
{
	nvgpu_log_fn(g, " ");

	if (class_num == PASCAL_COMPUTE_A) {
		switch (offset << 2) {
		case NVC0C0_SET_SHADER_EXCEPTIONS:
			gk20a_gr_set_shader_exceptions(g, data);
			break;
		case NVC0C0_SET_RD_COALESCE:
			gr_gm20b_set_rd_coalesce(g, data);
			break;
		default:
			goto fail;
		}
	}

	if (class_num == PASCAL_A) {
		switch (offset << 2) {
		case NVC097_SET_SHADER_EXCEPTIONS:
			gk20a_gr_set_shader_exceptions(g, data);
			break;
		case NVC097_SET_CIRCULAR_BUFFER_SIZE:
			g->ops.gr.set_circular_buffer_size(g, data);
			break;
		case NVC097_SET_ALPHA_CIRCULAR_BUFFER_SIZE:
			g->ops.gr.set_alpha_circular_buffer_size(g, data);
			break;
		case NVC097_SET_GO_IDLE_TIMEOUT:
			gr_gp10b_set_go_idle_timeout(g, data);
			break;
		case NVC097_SET_COALESCE_BUFFER_SIZE:
			gr_gp10b_set_coalesce_buffer_size(g, data);
			break;
		case NVC097_SET_RD_COALESCE:
			gr_gm20b_set_rd_coalesce(g, data);
			break;
		case NVC097_SET_BES_CROP_DEBUG3:
			g->ops.gr.set_bes_crop_debug3(g, data);
			break;
		case NVC097_SET_BES_CROP_DEBUG4:
			g->ops.gr.set_bes_crop_debug4(g, data);
			break;
		default:
			goto fail;
		}
	}
	return 0;

fail:
	return -EINVAL;
}

void gr_gp10b_cb_size_default(struct gk20a *g)
{
	struct gr_gk20a *gr = &g->gr;

	if (gr->attrib_cb_default_size == 0U) {
		gr->attrib_cb_default_size = 0x800;
	}
	gr->alpha_cb_default_size =
		gr_gpc0_ppc0_cbm_alpha_cb_size_v_default_v();
	gr->attrib_cb_gfxp_default_size =
		gr->attrib_cb_default_size +
			  (gr_gpc0_ppc0_cbm_beta_cb_size_v_gfxp_v() -
			   gr_gpc0_ppc0_cbm_beta_cb_size_v_default_v());
	gr->attrib_cb_gfxp_size =
		gr->attrib_cb_default_size +
			  (gr_gpc0_ppc0_cbm_beta_cb_size_v_gfxp_v() -
			   gr_gpc0_ppc0_cbm_beta_cb_size_v_default_v());
}

void gr_gp10b_set_alpha_circular_buffer_size(struct gk20a *g, u32 data)
{
	struct gr_gk20a *gr = &g->gr;
	u32 gpc_index, ppc_index, stride, val;
	u32 pd_ab_max_output;
	u32 alpha_cb_size = data * 4U;
	u32 gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_GPC_STRIDE);
	u32 ppc_in_gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_PPC_IN_GPC_STRIDE);

	nvgpu_log_fn(g, " ");

	if (alpha_cb_size > gr->alpha_cb_size) {
		alpha_cb_size = gr->alpha_cb_size;
	}

	gk20a_writel(g, gr_ds_tga_constraintlogic_alpha_r(),
		(gk20a_readl(g, gr_ds_tga_constraintlogic_alpha_r()) &
		 ~gr_ds_tga_constraintlogic_alpha_cbsize_f(~U32(0U))) |
		 gr_ds_tga_constraintlogic_alpha_cbsize_f(alpha_cb_size));

	pd_ab_max_output = alpha_cb_size *
		gr_gpc0_ppc0_cbm_alpha_cb_size_v_granularity_v() /
		gr_pd_ab_dist_cfg1_max_output_granularity_v();

	if (g->gr.pd_max_batches != 0U) {
		gk20a_writel(g, gr_pd_ab_dist_cfg1_r(),
			gr_pd_ab_dist_cfg1_max_output_f(pd_ab_max_output) |
			gr_pd_ab_dist_cfg1_max_batches_f(g->gr.pd_max_batches));
	} else {
		gk20a_writel(g, gr_pd_ab_dist_cfg1_r(),
			gr_pd_ab_dist_cfg1_max_output_f(pd_ab_max_output) |
			gr_pd_ab_dist_cfg1_max_batches_init_f());
	}

	for (gpc_index = 0;
	     gpc_index < nvgpu_gr_config_get_gpc_count(gr->config);
             gpc_index++) {
		stride = gpc_stride * gpc_index;

		for (ppc_index = 0;
		     ppc_index < nvgpu_gr_config_get_gpc_ppc_count(gr->config, gpc_index);
		     ppc_index++) {

			val = gk20a_readl(g, gr_gpc0_ppc0_cbm_alpha_cb_size_r() +
				stride +
				ppc_in_gpc_stride * ppc_index);

			val = set_field(val, gr_gpc0_ppc0_cbm_alpha_cb_size_v_m(),
					gr_gpc0_ppc0_cbm_alpha_cb_size_v_f(alpha_cb_size *
						nvgpu_gr_config_get_pes_tpc_count(gr->config,
							gpc_index, ppc_index)));

			gk20a_writel(g, gr_gpc0_ppc0_cbm_alpha_cb_size_r() +
				stride +
				ppc_in_gpc_stride * ppc_index, val);
		}
	}
}

void gr_gp10b_set_circular_buffer_size(struct gk20a *g, u32 data)
{
	struct gr_gk20a *gr = &g->gr;
	u32 gpc_index, ppc_index, stride, val;
	u32 cb_size_steady = data * 4U, cb_size;
	u32 gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_GPC_STRIDE);
	u32 ppc_in_gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_PPC_IN_GPC_STRIDE);

	nvgpu_log_fn(g, " ");

	if (cb_size_steady > gr->attrib_cb_size) {
		cb_size_steady = gr->attrib_cb_size;
	}
	if (gk20a_readl(g, gr_gpc0_ppc0_cbm_beta_cb_size_r()) !=
		gk20a_readl(g,
			gr_gpc0_ppc0_cbm_beta_steady_state_cb_size_r())) {
		cb_size = cb_size_steady +
			(gr_gpc0_ppc0_cbm_beta_cb_size_v_gfxp_v() -
			 gr_gpc0_ppc0_cbm_beta_cb_size_v_default_v());
	} else {
		cb_size = cb_size_steady;
	}

	gk20a_writel(g, gr_ds_tga_constraintlogic_beta_r(),
		(gk20a_readl(g, gr_ds_tga_constraintlogic_beta_r()) &
		 ~gr_ds_tga_constraintlogic_beta_cbsize_f(~U32(0U))) |
		 gr_ds_tga_constraintlogic_beta_cbsize_f(cb_size_steady));

	for (gpc_index = 0;
	     gpc_index < nvgpu_gr_config_get_gpc_count(gr->config);
	     gpc_index++) {
		stride = gpc_stride * gpc_index;

		for (ppc_index = 0;
		     ppc_index < nvgpu_gr_config_get_gpc_ppc_count(gr->config, gpc_index);
		     ppc_index++) {

			val = gk20a_readl(g, gr_gpc0_ppc0_cbm_beta_cb_size_r() +
				stride +
				ppc_in_gpc_stride * ppc_index);

			val = set_field(val,
				gr_gpc0_ppc0_cbm_beta_cb_size_v_m(),
				gr_gpc0_ppc0_cbm_beta_cb_size_v_f(cb_size *
					nvgpu_gr_config_get_pes_tpc_count(gr->config,
						gpc_index, ppc_index)));

			gk20a_writel(g, gr_gpc0_ppc0_cbm_beta_cb_size_r() +
				stride +
				ppc_in_gpc_stride * ppc_index, val);

			gk20a_writel(g, ppc_in_gpc_stride * ppc_index +
				gr_gpc0_ppc0_cbm_beta_steady_state_cb_size_r() +
				stride,
				gr_gpc0_ppc0_cbm_beta_steady_state_cb_size_v_f(
					cb_size_steady));

			val = gk20a_readl(g, gr_gpcs_swdx_tc_beta_cb_size_r(
						ppc_index + gpc_index));

			val = set_field(val,
				gr_gpcs_swdx_tc_beta_cb_size_v_m(),
				gr_gpcs_swdx_tc_beta_cb_size_v_f(
					cb_size_steady *
					nvgpu_gr_config_get_gpc_ppc_count(gr->config, gpc_index)));

			gk20a_writel(g, gr_gpcs_swdx_tc_beta_cb_size_r(
						ppc_index + gpc_index), val);
		}
	}
}

int gr_gp10b_init_ctx_state(struct gk20a *g)
{
	struct fecs_method_op_gk20a op = {
		.mailbox = { .id = 0U, .data = 0U,
			     .clr = ~U32(0U), .ok = 0U, .fail = 0U},
		.method.data = 0U,
		.cond.ok = GR_IS_UCODE_OP_NOT_EQUAL,
		.cond.fail = GR_IS_UCODE_OP_SKIP,
		};
	int err;

	nvgpu_log_fn(g, " ");

	err = gr_gk20a_init_ctx_state(g);
	if (err != 0) {
		return err;
	}

	if (g->gr.ctx_vars.preempt_image_size == 0U) {
		op.method.addr =
			gr_fecs_method_push_adr_discover_preemption_image_size_v();
		op.mailbox.ret = &g->gr.ctx_vars.preempt_image_size;
		err = gr_gk20a_submit_fecs_method_op(g, op, false);
		if (err != 0) {
			nvgpu_err(g, "query preempt image size failed");
			return err;
		}
	}

	nvgpu_log_info(g, "preempt image size: %u",
		g->gr.ctx_vars.preempt_image_size);

	nvgpu_log_fn(g, "done");

	return 0;
}

int gr_gp10b_set_ctxsw_preemption_mode(struct gk20a *g,
				struct nvgpu_gr_ctx *gr_ctx,
				struct vm_gk20a *vm, u32 class,
				u32 graphics_preempt_mode,
				u32 compute_preempt_mode)
{
	int err = 0;

	if (g->ops.gr.is_valid_gfx_class(g, class) &&
				g->gr.ctx_vars.force_preemption_gfxp) {
		graphics_preempt_mode = NVGPU_PREEMPTION_MODE_GRAPHICS_GFXP;
	}

	if (g->ops.gr.is_valid_compute_class(g, class) &&
			g->gr.ctx_vars.force_preemption_cilp) {
		compute_preempt_mode = NVGPU_PREEMPTION_MODE_COMPUTE_CILP;
	}

	/* check for invalid combinations */
	if ((graphics_preempt_mode == 0U) && (compute_preempt_mode == 0U)) {
		return -EINVAL;
	}

	if ((graphics_preempt_mode == NVGPU_PREEMPTION_MODE_GRAPHICS_GFXP) &&
		   (compute_preempt_mode == NVGPU_PREEMPTION_MODE_COMPUTE_CILP)) {
		return -EINVAL;
	}

	/* Do not allow lower preemption modes than current ones */
	if ((graphics_preempt_mode != 0U) &&
	    (graphics_preempt_mode < gr_ctx->graphics_preempt_mode)) {
		return -EINVAL;
	}

	if ((compute_preempt_mode != 0U) &&
	    (compute_preempt_mode < gr_ctx->compute_preempt_mode)) {
		return -EINVAL;
	}

	/* set preemption modes */
	switch (graphics_preempt_mode) {
	case NVGPU_PREEMPTION_MODE_GRAPHICS_GFXP:
		{
		u32 spill_size = g->ops.gr.get_ctx_spill_size(g);
		u32 pagepool_size = g->ops.gr.get_ctx_pagepool_size(g);
		u32 betacb_size = g->ops.gr.get_ctx_betacb_size(g);
		u32 attrib_cb_size =
			g->ops.gr.get_ctx_attrib_cb_size(g, betacb_size);
		attrib_cb_size = ALIGN(attrib_cb_size, 128);

		nvgpu_log_info(g, "gfxp context spill_size=%d", spill_size);
		nvgpu_log_info(g, "gfxp context pagepool_size=%d", pagepool_size);
		nvgpu_log_info(g, "gfxp context attrib_cb_size=%d",
				attrib_cb_size);

		nvgpu_gr_ctx_set_size(g->gr.gr_ctx_desc,
			NVGPU_GR_CTX_PREEMPT_CTXSW,
			g->gr.ctx_vars.preempt_image_size);
		nvgpu_gr_ctx_set_size(g->gr.gr_ctx_desc,
			NVGPU_GR_CTX_SPILL_CTXSW, spill_size);
		nvgpu_gr_ctx_set_size(g->gr.gr_ctx_desc,
			NVGPU_GR_CTX_BETACB_CTXSW, attrib_cb_size);
		nvgpu_gr_ctx_set_size(g->gr.gr_ctx_desc,
			NVGPU_GR_CTX_PAGEPOOL_CTXSW, pagepool_size);

		if (g->ops.gr.init_gfxp_rtv_cb != NULL) {
			err = g->ops.gr.init_gfxp_rtv_cb(g, gr_ctx, vm);
			if (err != 0) {
				nvgpu_err(g, "cannot allocate gfxp rtv_cb");
				goto fail;
			}
		}

		err = nvgpu_gr_ctx_alloc_ctxsw_buffers(g, gr_ctx,
			g->gr.gr_ctx_desc, vm);
		if (err != 0) {
			nvgpu_err(g, "cannot allocate ctxsw buffers");
			goto fail;
		}

		gr_ctx->graphics_preempt_mode = graphics_preempt_mode;
		break;
		}

	case NVGPU_PREEMPTION_MODE_GRAPHICS_WFI:
		gr_ctx->graphics_preempt_mode = graphics_preempt_mode;
		break;

	default:
		break;
	}

	if (g->ops.gr.is_valid_compute_class(g, class) ||
			g->ops.gr.is_valid_gfx_class(g, class)) {
		switch (compute_preempt_mode) {
		case NVGPU_PREEMPTION_MODE_COMPUTE_WFI:
		case NVGPU_PREEMPTION_MODE_COMPUTE_CTA:
		case NVGPU_PREEMPTION_MODE_COMPUTE_CILP:
			gr_ctx->compute_preempt_mode = compute_preempt_mode;
			break;
		default:
			break;
		}
	}

	return 0;

fail:
	return err;
}

int gr_gp10b_init_ctxsw_preemption_mode(struct gk20a *g,
	struct nvgpu_gr_ctx *gr_ctx, struct vm_gk20a *vm,
	u32 class, u32 flags)
{
	int err;
	u32 graphics_preempt_mode = 0;
	u32 compute_preempt_mode = 0;

	nvgpu_log_fn(g, " ");

	if ((flags & NVGPU_OBJ_CTX_FLAGS_SUPPORT_GFXP) != 0U) {
		graphics_preempt_mode = NVGPU_PREEMPTION_MODE_GRAPHICS_GFXP;
	}
	if ((flags & NVGPU_OBJ_CTX_FLAGS_SUPPORT_CILP) != 0U) {
		compute_preempt_mode = NVGPU_PREEMPTION_MODE_COMPUTE_CILP;
	}

	if ((graphics_preempt_mode != 0U) || (compute_preempt_mode != 0U)) {
		if (g->ops.gr.set_ctxsw_preemption_mode != NULL) {
			err = g->ops.gr.set_ctxsw_preemption_mode(g, gr_ctx, vm,
			    class, graphics_preempt_mode, compute_preempt_mode);
			if (err != 0) {
				nvgpu_err(g, "set_ctxsw_preemption_mode failed");
				return err;
			}
		} else {
			return -EINVAL;
		}
	}

	nvgpu_log_fn(g, "done");

	return 0;
}

void gr_gp10b_update_ctxsw_preemption_mode(struct gk20a *g,
		struct nvgpu_gr_ctx *gr_ctx, struct nvgpu_gr_subctx *subctx)
{
	struct nvgpu_mem *mem = &gr_ctx->mem;
	int err;

	nvgpu_log_fn(g, " ");

	if (gr_ctx->graphics_preempt_mode == NVGPU_PREEMPTION_MODE_GRAPHICS_GFXP) {
		g->ops.gr.ctxsw_prog.set_graphics_preemption_mode_gfxp(g, mem);
	}

	if (gr_ctx->compute_preempt_mode == NVGPU_PREEMPTION_MODE_COMPUTE_CILP) {
		g->ops.gr.ctxsw_prog.set_compute_preemption_mode_cilp(g, mem);
	}

	if (gr_ctx->compute_preempt_mode == NVGPU_PREEMPTION_MODE_COMPUTE_CTA) {
		g->ops.gr.ctxsw_prog.set_compute_preemption_mode_cta(g, mem);
	}

	if (gr_ctx->preempt_ctxsw_buffer.gpu_va != 0ULL) {
		u32 addr;
		u32 size;
		u32 cbes_reserve;

		if (g->ops.gr.set_preemption_buffer_va != NULL) {
			if (subctx != NULL) {
				g->ops.gr.set_preemption_buffer_va(g,
					&subctx->ctx_header,
					gr_ctx->preempt_ctxsw_buffer.gpu_va);
			} else {
				g->ops.gr.set_preemption_buffer_va(g, mem,
				gr_ctx->preempt_ctxsw_buffer.gpu_va);
			}
		}

		err = nvgpu_gr_ctx_patch_write_begin(g, gr_ctx, true);
		if (err != 0) {
			nvgpu_err(g, "can't map patch context");
			goto out;
		}

		addr = (u64_lo32(gr_ctx->betacb_ctxsw_buffer.gpu_va) >>
			gr_gpcs_setup_attrib_cb_base_addr_39_12_align_bits_v()) |
			(u64_hi32(gr_ctx->betacb_ctxsw_buffer.gpu_va) <<
			 (32U - gr_gpcs_setup_attrib_cb_base_addr_39_12_align_bits_v()));

		nvgpu_log_info(g, "attrib cb addr : 0x%016x", addr);
		g->ops.gr.commit_global_attrib_cb(g, gr_ctx, addr, true);

		addr = (u64_lo32(gr_ctx->pagepool_ctxsw_buffer.gpu_va) >>
			gr_scc_pagepool_base_addr_39_8_align_bits_v()) |
			(u64_hi32(gr_ctx->pagepool_ctxsw_buffer.gpu_va) <<
			 (32U - gr_scc_pagepool_base_addr_39_8_align_bits_v()));
		nvgpu_assert(gr_ctx->pagepool_ctxsw_buffer.size <= U32_MAX);
		size = (u32)gr_ctx->pagepool_ctxsw_buffer.size;

		if (size == g->ops.gr.pagepool_default_size(g)) {
			size = gr_scc_pagepool_total_pages_hwmax_v();
		}

		g->ops.gr.commit_global_pagepool(g, gr_ctx, addr, size, true);

		addr = (u64_lo32(gr_ctx->spill_ctxsw_buffer.gpu_va) >>
			gr_gpc0_swdx_rm_spill_buffer_addr_39_8_align_bits_v()) |
			(u64_hi32(gr_ctx->spill_ctxsw_buffer.gpu_va) <<
			 (32U - gr_gpc0_swdx_rm_spill_buffer_addr_39_8_align_bits_v()));
		nvgpu_assert(gr_ctx->spill_ctxsw_buffer.size <= U32_MAX);
		size = (u32)gr_ctx->spill_ctxsw_buffer.size /
			gr_gpc0_swdx_rm_spill_buffer_size_256b_byte_granularity_v();

		nvgpu_gr_ctx_patch_write(g, gr_ctx,
				gr_gpc0_swdx_rm_spill_buffer_addr_r(),
				gr_gpc0_swdx_rm_spill_buffer_addr_39_8_f(addr),
				true);
		nvgpu_gr_ctx_patch_write(g, gr_ctx,
				gr_gpc0_swdx_rm_spill_buffer_size_r(),
				gr_gpc0_swdx_rm_spill_buffer_size_256b_f(size),
				true);

		cbes_reserve = gr_gpcs_swdx_beta_cb_ctrl_cbes_reserve_gfxp_v();
		nvgpu_gr_ctx_patch_write(g, gr_ctx,
				gr_gpcs_swdx_beta_cb_ctrl_r(),
				gr_gpcs_swdx_beta_cb_ctrl_cbes_reserve_f(
					cbes_reserve),
				true);
		nvgpu_gr_ctx_patch_write(g, gr_ctx,
				gr_gpcs_ppcs_cbm_beta_cb_ctrl_r(),
				gr_gpcs_ppcs_cbm_beta_cb_ctrl_cbes_reserve_f(
					cbes_reserve),
				true);

		nvgpu_gr_ctx_patch_write_end(g, gr_ctx, true);
	}

out:
	nvgpu_log_fn(g, "done");
}

int gr_gp10b_dump_gr_status_regs(struct gk20a *g,
			   struct gk20a_debug_output *o)
{
	struct gr_gk20a *gr = &g->gr;
	u32 gr_engine_id;
	struct nvgpu_engine_status_info engine_status;

	gr_engine_id = nvgpu_engine_get_gr_eng_id(g);

	gk20a_debug_output(o, "NV_PGRAPH_STATUS: 0x%x\n",
		gk20a_readl(g, gr_status_r()));
	gk20a_debug_output(o, "NV_PGRAPH_STATUS1: 0x%x\n",
		gk20a_readl(g, gr_status_1_r()));
	gk20a_debug_output(o, "NV_PGRAPH_STATUS2: 0x%x\n",
		gk20a_readl(g, gr_status_2_r()));
	gk20a_debug_output(o, "NV_PGRAPH_ENGINE_STATUS: 0x%x\n",
		gk20a_readl(g, gr_engine_status_r()));
	gk20a_debug_output(o, "NV_PGRAPH_GRFIFO_STATUS : 0x%x\n",
		gk20a_readl(g, gr_gpfifo_status_r()));
	gk20a_debug_output(o, "NV_PGRAPH_GRFIFO_CONTROL : 0x%x\n",
		gk20a_readl(g, gr_gpfifo_ctl_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_FECS_HOST_INT_STATUS : 0x%x\n",
		gk20a_readl(g, gr_fecs_host_int_status_r()));
	gk20a_debug_output(o, "NV_PGRAPH_EXCEPTION  : 0x%x\n",
		gk20a_readl(g, gr_exception_r()));
	gk20a_debug_output(o, "NV_PGRAPH_FECS_INTR  : 0x%x\n",
		gk20a_readl(g, gr_fecs_intr_r()));
	g->ops.engine_status.read_engine_status_info(g, gr_engine_id,
		&engine_status);
	gk20a_debug_output(o, "NV_PFIFO_ENGINE_STATUS(GR) : 0x%x\n",
		engine_status.reg_data);
	gk20a_debug_output(o, "NV_PGRAPH_ACTIVITY0: 0x%x\n",
		gk20a_readl(g, gr_activity_0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_ACTIVITY1: 0x%x\n",
		gk20a_readl(g, gr_activity_1_r()));
	gk20a_debug_output(o, "NV_PGRAPH_ACTIVITY2: 0x%x\n",
		gk20a_readl(g, gr_activity_2_r()));
	gk20a_debug_output(o, "NV_PGRAPH_ACTIVITY4: 0x%x\n",
		gk20a_readl(g, gr_activity_4_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_SKED_ACTIVITY: 0x%x\n",
		gk20a_readl(g, gr_pri_sked_activity_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_GPCCS_GPC_ACTIVITY0: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_gpccs_gpc_activity0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_GPCCS_GPC_ACTIVITY1: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_gpccs_gpc_activity1_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_GPCCS_GPC_ACTIVITY2: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_gpccs_gpc_activity2_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_GPCCS_GPC_ACTIVITY3: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_gpccs_gpc_activity3_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_TPC0_TPCCS_TPC_ACTIVITY0: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_tpc0_tpccs_tpc_activity_0_r()));
	if ((gr->config->gpc_tpc_count != NULL) && (gr->config->gpc_tpc_count[0] == 2U)) {
		gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_TPC1_TPCCS_TPC_ACTIVITY0: 0x%x\n",
			gk20a_readl(g, gr_pri_gpc0_tpc1_tpccs_tpc_activity_0_r()));
	}
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_TPCS_TPCCS_TPC_ACTIVITY0: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_tpcs_tpccs_tpc_activity_0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPCS_GPCCS_GPC_ACTIVITY0: 0x%x\n",
		gk20a_readl(g, gr_pri_gpcs_gpccs_gpc_activity_0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPCS_GPCCS_GPC_ACTIVITY1: 0x%x\n",
		gk20a_readl(g, gr_pri_gpcs_gpccs_gpc_activity_1_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPCS_GPCCS_GPC_ACTIVITY2: 0x%x\n",
		gk20a_readl(g, gr_pri_gpcs_gpccs_gpc_activity_2_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPCS_GPCCS_GPC_ACTIVITY3: 0x%x\n",
		gk20a_readl(g, gr_pri_gpcs_gpccs_gpc_activity_3_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPCS_TPC0_TPCCS_TPC_ACTIVITY0: 0x%x\n",
		gk20a_readl(g, gr_pri_gpcs_tpc0_tpccs_tpc_activity_0_r()));
	if ((gr->config->gpc_tpc_count != NULL) && (gr->config->gpc_tpc_count[0] == 2U)) {
		gk20a_debug_output(o, "NV_PGRAPH_PRI_GPCS_TPC1_TPCCS_TPC_ACTIVITY0: 0x%x\n",
			gk20a_readl(g, gr_pri_gpcs_tpc1_tpccs_tpc_activity_0_r()));
	}
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPCS_TPCS_TPCCS_TPC_ACTIVITY0: 0x%x\n",
		gk20a_readl(g, gr_pri_gpcs_tpcs_tpccs_tpc_activity_0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BE0_BECS_BE_ACTIVITY0: 0x%x\n",
		gk20a_readl(g, gr_pri_be0_becs_be_activity0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BE1_BECS_BE_ACTIVITY0: 0x%x\n",
		gk20a_readl(g, gr_pri_be1_becs_be_activity0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BES_BECS_BE_ACTIVITY0: 0x%x\n",
		gk20a_readl(g, gr_pri_bes_becs_be_activity0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_DS_MPIPE_STATUS: 0x%x\n",
		gk20a_readl(g, gr_pri_ds_mpipe_status_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_FE_GO_IDLE_TIMEOUT : 0x%x\n",
		gk20a_readl(g, gr_fe_go_idle_timeout_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_FE_GO_IDLE_INFO : 0x%x\n",
		gk20a_readl(g, gr_pri_fe_go_idle_info_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_TPC0_TEX_M_TEX_SUBUNITS_STATUS: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_tpc0_tex_m_tex_subunits_status_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_CWD_FS: 0x%x\n",
		gk20a_readl(g, gr_cwd_fs_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_FE_TPC_FS: 0x%x\n",
		gk20a_readl(g, gr_fe_tpc_fs_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_CWD_GPC_TPC_ID(0): 0x%x\n",
		gk20a_readl(g, gr_cwd_gpc_tpc_id_r(0)));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_CWD_SM_ID(0): 0x%x\n",
		gk20a_readl(g, gr_cwd_sm_id_r(0)));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_FECS_CTXSW_STATUS_FE_0: 0x%x\n",
		gk20a_readl(g, gr_fecs_ctxsw_status_fe_0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_FECS_CTXSW_STATUS_1: 0x%x\n",
		gk20a_readl(g, gr_fecs_ctxsw_status_1_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_GPCCS_CTXSW_STATUS_GPC_0: 0x%x\n",
		gk20a_readl(g, gr_gpc0_gpccs_ctxsw_status_gpc_0_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_GPCCS_CTXSW_STATUS_1: 0x%x\n",
		gk20a_readl(g, gr_gpc0_gpccs_ctxsw_status_1_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_FECS_CTXSW_IDLESTATE : 0x%x\n",
		gk20a_readl(g, gr_fecs_ctxsw_idlestate_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_GPCCS_CTXSW_IDLESTATE : 0x%x\n",
		gk20a_readl(g, gr_gpc0_gpccs_ctxsw_idlestate_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_FECS_CURRENT_CTX : 0x%x\n",
		gk20a_readl(g, gr_fecs_current_ctx_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_FECS_NEW_CTX : 0x%x\n",
		gk20a_readl(g, gr_fecs_new_ctx_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BE0_CROP_STATUS1 : 0x%x\n",
		gk20a_readl(g, gr_pri_be0_crop_status1_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BES_CROP_STATUS1 : 0x%x\n",
		gk20a_readl(g, gr_pri_bes_crop_status1_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BE0_ZROP_STATUS : 0x%x\n",
		gk20a_readl(g, gr_pri_be0_zrop_status_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BE0_ZROP_STATUS2 : 0x%x\n",
		gk20a_readl(g, gr_pri_be0_zrop_status2_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BES_ZROP_STATUS : 0x%x\n",
		gk20a_readl(g, gr_pri_bes_zrop_status_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BES_ZROP_STATUS2 : 0x%x\n",
		gk20a_readl(g, gr_pri_bes_zrop_status2_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BE0_BECS_BE_EXCEPTION: 0x%x\n",
		gk20a_readl(g, gr_pri_be0_becs_be_exception_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_BE0_BECS_BE_EXCEPTION_EN: 0x%x\n",
		gk20a_readl(g, gr_pri_be0_becs_be_exception_en_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_GPCCS_GPC_EXCEPTION: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_gpccs_gpc_exception_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_GPCCS_GPC_EXCEPTION_EN: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_gpccs_gpc_exception_en_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_TPC0_TPCCS_TPC_EXCEPTION: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_tpc0_tpccs_tpc_exception_r()));
	gk20a_debug_output(o, "NV_PGRAPH_PRI_GPC0_TPC0_TPCCS_TPC_EXCEPTION_EN: 0x%x\n",
		gk20a_readl(g, gr_pri_gpc0_tpc0_tpccs_tpc_exception_en_r()));
	return 0;
}

static bool gr_activity_empty_or_preempted(u32 val)
{
	while(val != 0U) {
		u32 v = val & 7U;
		if (v != gr_activity_4_gpc0_empty_v() &&
		    v != gr_activity_4_gpc0_preempted_v()) {
			return false;
		}
		val >>= 3;
	}

	return true;
}

int gr_gp10b_wait_empty(struct gk20a *g)
{
	u32 delay = GR_IDLE_CHECK_DEFAULT;
	bool ctxsw_active;
	bool gr_busy;
	u32 gr_status;
	u32 activity0, activity1, activity2, activity4;
	struct nvgpu_timeout timeout;

	nvgpu_log_fn(g, " ");

	nvgpu_timeout_init(g, &timeout, gk20a_get_gr_idle_timeout(g),
			   NVGPU_TIMER_CPU_TIMER);

	do {
		/* fmodel: host gets fifo_engine_status(gr) from gr
		   only when gr_status is read */
		gr_status = gk20a_readl(g, gr_status_r());

		ctxsw_active = (gr_status & BIT32(7)) != 0U;

		activity0 = gk20a_readl(g, gr_activity_0_r());
		activity1 = gk20a_readl(g, gr_activity_1_r());
		activity2 = gk20a_readl(g, gr_activity_2_r());
		activity4 = gk20a_readl(g, gr_activity_4_r());

		gr_busy = !(gr_activity_empty_or_preempted(activity0) &&
			    gr_activity_empty_or_preempted(activity1) &&
			    activity2 == 0U &&
			    gr_activity_empty_or_preempted(activity4));

		if (!gr_busy && !ctxsw_active) {
			nvgpu_log_fn(g, "done");
			return 0;
		}

		nvgpu_usleep_range(delay, delay * 2U);
		delay = min_t(u32, delay << 1, GR_IDLE_CHECK_MAX);
	} while (nvgpu_timeout_expired(&timeout) == 0);

	nvgpu_err(g,
		"timeout, ctxsw busy : %d, gr busy : %d, %08x, %08x, %08x, %08x",
		ctxsw_active, gr_busy, activity0, activity1, activity2, activity4);

	return -EAGAIN;
}

void gr_gp10b_commit_global_attrib_cb(struct gk20a *g,
					     struct nvgpu_gr_ctx *gr_ctx,
					     u64 addr, bool patch)
{
	u32 attrBufferSize;

	if (gr_ctx->preempt_ctxsw_buffer.gpu_va != 0ULL) {
		attrBufferSize = U32(gr_ctx->betacb_ctxsw_buffer.size);
	} else {
		attrBufferSize = g->ops.gr.calc_global_ctx_buffer_size(g);
	}

	attrBufferSize /= gr_gpcs_tpcs_tex_rm_cb_1_size_div_128b_granularity_f();

	gr_gm20b_commit_global_attrib_cb(g, gr_ctx, addr, patch);

	nvgpu_assert(u64_hi32(addr) == 0U);
	nvgpu_gr_ctx_patch_write(g, gr_ctx, gr_gpcs_tpcs_mpc_vtg_cb_global_base_addr_r(),
		gr_gpcs_tpcs_mpc_vtg_cb_global_base_addr_v_f((u32)addr) |
		gr_gpcs_tpcs_mpc_vtg_cb_global_base_addr_valid_true_f(), patch);

	nvgpu_gr_ctx_patch_write(g, gr_ctx, gr_gpcs_tpcs_tex_rm_cb_0_r(),
		gr_gpcs_tpcs_tex_rm_cb_0_base_addr_43_12_f((u32)addr), patch);

	nvgpu_gr_ctx_patch_write(g, gr_ctx, gr_gpcs_tpcs_tex_rm_cb_1_r(),
		gr_gpcs_tpcs_tex_rm_cb_1_size_div_128b_f(attrBufferSize) |
		gr_gpcs_tpcs_tex_rm_cb_1_valid_true_f(), patch);
}

void gr_gp10b_commit_global_bundle_cb(struct gk20a *g,
					    struct nvgpu_gr_ctx *gr_ctx,
					    u64 addr, u64 size, bool patch)
{
	u32 data;

	nvgpu_assert(u64_hi32(addr) == 0U);
	nvgpu_gr_ctx_patch_write(g, gr_ctx, gr_scc_bundle_cb_base_r(),
		gr_scc_bundle_cb_base_addr_39_8_f((u32)addr), patch);

	nvgpu_assert(size <= U32_MAX);
	nvgpu_gr_ctx_patch_write(g, gr_ctx, gr_scc_bundle_cb_size_r(),
		gr_scc_bundle_cb_size_div_256b_f((u32)size) |
		gr_scc_bundle_cb_size_valid_true_f(), patch);

	nvgpu_gr_ctx_patch_write(g, gr_ctx, gr_gpcs_swdx_bundle_cb_base_r(),
		gr_gpcs_swdx_bundle_cb_base_addr_39_8_f((u32)addr), patch);

	nvgpu_gr_ctx_patch_write(g, gr_ctx, gr_gpcs_swdx_bundle_cb_size_r(),
		gr_gpcs_swdx_bundle_cb_size_div_256b_f((u32)size) |
		gr_gpcs_swdx_bundle_cb_size_valid_true_f(), patch);

	/* data for state_limit */
	data = (g->gr.bundle_cb_default_size *
		gr_scc_bundle_cb_size_div_256b_byte_granularity_v()) /
		gr_pd_ab_dist_cfg2_state_limit_scc_bundle_granularity_v();

	data = min_t(u32, data, g->gr.min_gpm_fifo_depth);

	nvgpu_log_info(g, "bundle cb token limit : %d, state limit : %d",
		   g->gr.bundle_cb_token_limit, data);

	nvgpu_gr_ctx_patch_write(g, gr_ctx, gr_pd_ab_dist_cfg2_r(),
		gr_pd_ab_dist_cfg2_token_limit_f(g->gr.bundle_cb_token_limit) |
		gr_pd_ab_dist_cfg2_state_limit_f(data), patch);
}

int gr_gp10b_load_smid_config(struct gk20a *g)
{
	u32 *tpc_sm_id;
	u32 i, j;
	u32 tpc_index, gpc_index;
	u32 max_gpcs = nvgpu_get_litter_value(g, GPU_LIT_NUM_GPCS);

	tpc_sm_id = nvgpu_kcalloc(g, gr_cwd_sm_id__size_1_v(), sizeof(u32));
	if (tpc_sm_id == NULL) {
		return -ENOMEM;
	}

	/* Each NV_PGRAPH_PRI_CWD_GPC_TPC_ID can store 4 TPCs.*/
	for (i = 0U;
	     i <= ((nvgpu_gr_config_get_tpc_count(g->gr.config) - 1U) / 4U);
	     i++) {
		u32 reg = 0;
		u32 bit_stride = gr_cwd_gpc_tpc_id_gpc0_s() +
				 gr_cwd_gpc_tpc_id_tpc0_s();

		for (j = 0U; j < 4U; j++) {
			u32 sm_id = (i * 4U) + j;
			u32 bits;

			if (sm_id >= nvgpu_gr_config_get_tpc_count(g->gr.config)) {
				break;
			}

			gpc_index = g->gr.sm_to_cluster[sm_id].gpc_index;
			tpc_index = g->gr.sm_to_cluster[sm_id].tpc_index;

			bits = gr_cwd_gpc_tpc_id_gpc0_f(gpc_index) |
			       gr_cwd_gpc_tpc_id_tpc0_f(tpc_index);
			reg |= bits << (j * bit_stride);

			tpc_sm_id[gpc_index + max_gpcs * ((tpc_index & 4U) >> 2U)]
				|= sm_id << (bit_stride * (tpc_index & 3U));
		}
		gk20a_writel(g, gr_cwd_gpc_tpc_id_r(i), reg);
	}

	for (i = 0; i < gr_cwd_sm_id__size_1_v(); i++) {
		gk20a_writel(g, gr_cwd_sm_id_r(i), tpc_sm_id[i]);
	}

	nvgpu_kfree(g, tpc_sm_id);

	return 0;
}

int gr_gp10b_init_fs_state(struct gk20a *g)
{
	u32 data;

	nvgpu_log_fn(g, " ");

	data = gk20a_readl(g, gr_gpcs_tpcs_sm_texio_control_r());
	data = set_field(data, gr_gpcs_tpcs_sm_texio_control_oor_addr_check_mode_m(),
			gr_gpcs_tpcs_sm_texio_control_oor_addr_check_mode_arm_63_48_match_f());
	gk20a_writel(g, gr_gpcs_tpcs_sm_texio_control_r(), data);

	data = gk20a_readl(g, gr_gpcs_tpcs_sm_disp_ctrl_r());
	data = set_field(data, gr_gpcs_tpcs_sm_disp_ctrl_re_suppress_m(),
			 gr_gpcs_tpcs_sm_disp_ctrl_re_suppress_disable_f());
	gk20a_writel(g, gr_gpcs_tpcs_sm_disp_ctrl_r(), data);

	if (g->gr.fecs_feature_override_ecc_val != 0U) {
		gk20a_writel(g,
			gr_fecs_feature_override_ecc_r(),
			g->gr.fecs_feature_override_ecc_val);
	}

	return gr_gm20b_init_fs_state(g);
}

void gr_gp10b_set_gpc_tpc_mask(struct gk20a *g, u32 gpc_index)
{
	nvgpu_tegra_fuse_write_bypass(g, 0x1);
	nvgpu_tegra_fuse_write_access_sw(g, 0x0);

	if (nvgpu_gr_config_get_gpc_tpc_mask(g->gr.config, gpc_index) == 0x1U) {
		nvgpu_tegra_fuse_write_opt_gpu_tpc0_disable(g, 0x2);
	} else if (nvgpu_gr_config_get_gpc_tpc_mask(g->gr.config, gpc_index) ==
			0x2U) {
		nvgpu_tegra_fuse_write_opt_gpu_tpc0_disable(g, 0x1);
	} else {
		nvgpu_tegra_fuse_write_opt_gpu_tpc0_disable(g, 0x0);
	}
}

void gr_gp10b_get_access_map(struct gk20a *g,
				   u32 **whitelist, int *num_entries)
{
	static u32 wl_addr_gp10b[] = {
		/* this list must be sorted (low to high) */
		0x404468, /* gr_pri_mme_max_instructions       */
		0x418300, /* gr_pri_gpcs_rasterarb_line_class  */
		0x418800, /* gr_pri_gpcs_setup_debug           */
		0x418e00, /* gr_pri_gpcs_swdx_config           */
		0x418e40, /* gr_pri_gpcs_swdx_tc_bundle_ctrl   */
		0x418e44, /* gr_pri_gpcs_swdx_tc_bundle_ctrl   */
		0x418e48, /* gr_pri_gpcs_swdx_tc_bundle_ctrl   */
		0x418e4c, /* gr_pri_gpcs_swdx_tc_bundle_ctrl   */
		0x418e50, /* gr_pri_gpcs_swdx_tc_bundle_ctrl   */
		0x418e58, /* gr_pri_gpcs_swdx_tc_bundle_addr   */
		0x418e5c, /* gr_pri_gpcs_swdx_tc_bundle_addr   */
		0x418e60, /* gr_pri_gpcs_swdx_tc_bundle_addr   */
		0x418e64, /* gr_pri_gpcs_swdx_tc_bundle_addr   */
		0x418e68, /* gr_pri_gpcs_swdx_tc_bundle_addr   */
		0x418e6c, /* gr_pri_gpcs_swdx_tc_bundle_addr   */
		0x418e70, /* gr_pri_gpcs_swdx_tc_bundle_addr   */
		0x418e74, /* gr_pri_gpcs_swdx_tc_bundle_addr   */
		0x418e78, /* gr_pri_gpcs_swdx_tc_bundle_addr   */
		0x418e7c, /* gr_pri_gpcs_swdx_tc_bundle_addr   */
		0x418e80, /* gr_pri_gpcs_swdx_tc_bundle_addr   */
		0x418e84, /* gr_pri_gpcs_swdx_tc_bundle_addr   */
		0x418e88, /* gr_pri_gpcs_swdx_tc_bundle_addr   */
		0x418e8c, /* gr_pri_gpcs_swdx_tc_bundle_addr   */
		0x418e90, /* gr_pri_gpcs_swdx_tc_bundle_addr   */
		0x418e94, /* gr_pri_gpcs_swdx_tc_bundle_addr   */
		0x419864, /* gr_pri_gpcs_tpcs_pe_l2_evict_policy */
		0x419a04, /* gr_pri_gpcs_tpcs_tex_lod_dbg      */
		0x419a08, /* gr_pri_gpcs_tpcs_tex_samp_dbg     */
		0x419e10, /* gr_pri_gpcs_tpcs_sm_dbgr_control0 */
		0x419f78, /* gr_pri_gpcs_tpcs_sm_disp_ctrl     */
	};
	size_t array_size;

	*whitelist = wl_addr_gp10b;
	array_size = ARRAY_SIZE(wl_addr_gp10b);
	*num_entries = (int)array_size;
}

static int gr_gp10b_disable_channel_or_tsg(struct gk20a *g, struct channel_gk20a *fault_ch)
{
	int ret = 0;
	struct tsg_gk20a *tsg;

	tsg = tsg_gk20a_from_ch(fault_ch);
	if (tsg == NULL) {
		nvgpu_err(g, "CILP: chid: %d is not bound to tsg",
				fault_ch->chid);
		return -EINVAL;
	}

	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg | gpu_dbg_intr, " ");

	ret = gk20a_disable_channel_tsg(g, fault_ch);
	if (ret != 0) {
		nvgpu_err(g,
				"CILP: failed to disable channel/TSG!");
		return ret;
	}

	ret = g->ops.runlist.reload(g, fault_ch->runlist_id, true, false);
	if (ret != 0) {
		nvgpu_err(g,
				"CILP: failed to restart runlist 0!");
		return ret;
	}

	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg | gpu_dbg_intr, "CILP: restarted runlist");

	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg | gpu_dbg_intr,
			"CILP: tsgid: 0x%x", tsg->tsgid);

	gk20a_fifo_issue_preempt(g, tsg->tsgid, true);
	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg | gpu_dbg_intr,
			"CILP: preempted tsg");
	return ret;
}

int gr_gp10b_set_cilp_preempt_pending(struct gk20a *g,
			struct channel_gk20a *fault_ch)
{
	int ret;
	struct tsg_gk20a *tsg;
	struct nvgpu_gr_ctx *gr_ctx;

	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg | gpu_dbg_intr, " ");

	tsg = tsg_gk20a_from_ch(fault_ch);
	if (tsg == NULL) {
		return -EINVAL;
	}

	gr_ctx = tsg->gr_ctx;

	if (gr_ctx->cilp_preempt_pending) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg | gpu_dbg_intr,
				"CILP is already pending for chid %d",
				fault_ch->chid);
		return 0;
	}

	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg | gpu_dbg_intr,
			"CILP: ctx id is 0x%x", gr_ctx->ctx_id);

	/* send ucode method to set ctxsw interrupt */
	ret = gr_gk20a_submit_fecs_sideband_method_op(g,
			(struct fecs_method_op_gk20a) {
			.method.data = nvgpu_gr_ctx_get_ctx_id(g, gr_ctx),
			.method.addr =
			gr_fecs_method_push_adr_configure_interrupt_completion_option_v(),
			.mailbox = {
			.id = 1U /* sideband */, .data = 0U,
			.clr = ~U32(0U), .ret = NULL,
			.ok = gr_fecs_ctxsw_mailbox_value_pass_v(),
			.fail = 0U},
			.cond.ok = GR_IS_UCODE_OP_EQUAL,
			.cond.fail = GR_IS_UCODE_OP_SKIP});

	if (ret != 0) {
		nvgpu_err(g, "CILP: failed to enable ctxsw interrupt!");
		return ret;
	}

	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg | gpu_dbg_intr,
				"CILP: enabled ctxsw completion interrupt");

	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg | gpu_dbg_intr,
			"CILP: disabling channel %d",
			fault_ch->chid);

	ret = gr_gp10b_disable_channel_or_tsg(g, fault_ch);
	if (ret != 0) {
		nvgpu_err(g, "CILP: failed to disable channel!!");
		return ret;
	}

	/* set cilp_preempt_pending = true and record the channel */
	gr_ctx->cilp_preempt_pending = true;
	g->gr.cilp_preempt_pending_chid = fault_ch->chid;

	g->ops.fifo.post_event_id(tsg,
				NVGPU_EVENT_ID_CILP_PREEMPTION_STARTED);

	return 0;
}

static int gr_gp10b_clear_cilp_preempt_pending(struct gk20a *g,
					       struct channel_gk20a *fault_ch)
{
	struct tsg_gk20a *tsg;
	struct nvgpu_gr_ctx *gr_ctx;

	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg | gpu_dbg_intr, " ");

	tsg = tsg_gk20a_from_ch(fault_ch);
	if (tsg == NULL) {
		return -EINVAL;
	}

	gr_ctx = tsg->gr_ctx;

	/* The ucode is self-clearing, so all we need to do here is
	   to clear cilp_preempt_pending. */
	if (!gr_ctx->cilp_preempt_pending) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg | gpu_dbg_intr,
				"CILP is already cleared for chid %d\n",
				fault_ch->chid);
		return 0;
	}

	gr_ctx->cilp_preempt_pending = false;
	g->gr.cilp_preempt_pending_chid = FIFO_INVAL_CHANNEL_ID;

	return 0;
}

/* @brief pre-process work on the SM exceptions to determine if we clear them or not.
 *
 * On Pascal, if we are in CILP preemtion mode, preempt the channel and handle errors with special processing
 */
int gr_gp10b_pre_process_sm_exception(struct gk20a *g,
		u32 gpc, u32 tpc, u32 sm, u32 global_esr, u32 warp_esr,
		bool sm_debugger_attached, struct channel_gk20a *fault_ch,
		bool *early_exit, bool *ignore_debugger)
{
#ifdef NVGPU_DEBUGGER
	bool cilp_enabled = false;
	struct tsg_gk20a *tsg;

	*early_exit = false;
	*ignore_debugger = false;

	if (fault_ch != NULL) {
		tsg = tsg_gk20a_from_ch(fault_ch);
		if (tsg  == NULL) {
			return -EINVAL;
		}

		cilp_enabled = (tsg->gr_ctx->compute_preempt_mode ==
			NVGPU_PREEMPTION_MODE_COMPUTE_CILP);
	}

	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg, "SM Exception received on gpc %d tpc %d = %u\n",
			gpc, tpc, global_esr);

	if (cilp_enabled && sm_debugger_attached) {
		u32 gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_GPC_STRIDE);
		u32 tpc_in_gpc_stride = nvgpu_get_litter_value(g, GPU_LIT_TPC_IN_GPC_STRIDE);
		u32 offset = gpc_stride * gpc + tpc_in_gpc_stride * tpc;
		u32 global_mask = 0, dbgr_control0, global_esr_copy;
		int ret;

		if ((global_esr & gr_gpc0_tpc0_sm_hww_global_esr_bpt_int_pending_f()) != 0U) {
			gk20a_writel(g, gr_gpc0_tpc0_sm_hww_global_esr_r() + offset,
					gr_gpc0_tpc0_sm_hww_global_esr_bpt_int_pending_f());
		}

		if ((global_esr & gr_gpc0_tpc0_sm_hww_global_esr_single_step_complete_pending_f()) != 0U) {
			gk20a_writel(g, gr_gpc0_tpc0_sm_hww_global_esr_r() + offset,
					gr_gpc0_tpc0_sm_hww_global_esr_single_step_complete_pending_f());
		}

		global_mask = gr_gpc0_tpc0_sm_hww_global_esr_sm_to_sm_fault_pending_f() |
			gr_gpcs_tpcs_sm_hww_global_esr_l1_error_pending_f() |
			gr_gpcs_tpcs_sm_hww_global_esr_multiple_warp_errors_pending_f() |
			gr_gpcs_tpcs_sm_hww_global_esr_physical_stack_overflow_error_pending_f() |
			gr_gpcs_tpcs_sm_hww_global_esr_timeout_error_pending_f() |
			gr_gpcs_tpcs_sm_hww_global_esr_bpt_pause_pending_f();

		if (warp_esr != 0U || (global_esr & global_mask) != 0U) {
			*ignore_debugger = true;

			nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg,
					"CILP: starting wait for LOCKED_DOWN on gpc %d tpc %d\n",
					gpc, tpc);

			if (nvgpu_dbg_gpu_broadcast_stop_trigger(fault_ch)) {
				nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg,
						"CILP: Broadcasting STOP_TRIGGER from gpc %d tpc %d\n",
						gpc, tpc);
				g->ops.gr.suspend_all_sms(g, global_mask, false);

				nvgpu_dbg_gpu_clear_broadcast_stop_trigger(fault_ch);
			} else {
				nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg,
						"CILP: STOP_TRIGGER from gpc %d tpc %d\n",
						gpc, tpc);
				g->ops.gr.suspend_single_sm(g, gpc, tpc, sm, global_mask, true);
			}

			/* reset the HWW errors after locking down */
			global_esr_copy = g->ops.gr.get_sm_hww_global_esr(g,
							gpc, tpc, sm);
			g->ops.gr.clear_sm_hww(g,
						gpc, tpc, sm, global_esr_copy);
			nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg,
					"CILP: HWWs cleared for gpc %d tpc %d\n",
					gpc, tpc);

			nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg, "CILP: Setting CILP preempt pending\n");
			ret = gr_gp10b_set_cilp_preempt_pending(g, fault_ch);
			if (ret != 0) {
				nvgpu_err(g, "CILP: error while setting CILP preempt pending!");
				return ret;
			}

			dbgr_control0 = gk20a_readl(g, gr_gpc0_tpc0_sm_dbgr_control0_r() + offset);
			if ((dbgr_control0 & gr_gpcs_tpcs_sm_dbgr_control0_single_step_mode_enable_f()) != 0U) {
				nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg,
						"CILP: clearing SINGLE_STEP_MODE before resume for gpc %d tpc %d\n",
						gpc, tpc);
				dbgr_control0 = set_field(dbgr_control0,
						gr_gpcs_tpcs_sm_dbgr_control0_single_step_mode_m(),
						gr_gpcs_tpcs_sm_dbgr_control0_single_step_mode_disable_f());
				gk20a_writel(g, gr_gpc0_tpc0_sm_dbgr_control0_r() + offset, dbgr_control0);
			}

			nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg,
					"CILP: resume for gpc %d tpc %d\n",
					gpc, tpc);
			g->ops.gr.resume_single_sm(g, gpc, tpc, sm);

			*ignore_debugger = true;
			nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg, "CILP: All done on gpc %d, tpc %d\n", gpc, tpc);
		}

		*early_exit = true;
	}
#endif
	return 0;
}

static int gr_gp10b_get_cilp_preempt_pending_chid(struct gk20a *g, u32 *__chid)
{
	struct nvgpu_gr_ctx *gr_ctx;
	struct channel_gk20a *ch;
	struct tsg_gk20a *tsg;
	u32 chid;
	int ret = -EINVAL;

	chid = g->gr.cilp_preempt_pending_chid;
	if (chid == FIFO_INVAL_CHANNEL_ID) {
		return ret;
	}

	ch = gk20a_channel_from_id(g, chid);
	if (ch == NULL) {
		return ret;
	}

	tsg = tsg_gk20a_from_ch(ch);
	if (tsg == NULL) {
		gk20a_channel_put(ch);
		return -EINVAL;
	}

	gr_ctx = tsg->gr_ctx;

	if (gr_ctx->cilp_preempt_pending) {
		*__chid = chid;
		ret = 0;
	}

	gk20a_channel_put(ch);

	return ret;
}

int gr_gp10b_handle_fecs_error(struct gk20a *g,
				struct channel_gk20a *__ch,
				struct gr_gk20a_isr_data *isr_data)
{
	u32 gr_fecs_intr = gk20a_readl(g, gr_fecs_host_int_status_r());
	struct channel_gk20a *ch;
	u32 chid = FIFO_INVAL_CHANNEL_ID;
	int ret = 0;
	struct tsg_gk20a *tsg;

	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg | gpu_dbg_intr, " ");

	/*
	 * INTR1 (bit 1 of the HOST_INT_STATUS_CTXSW_INTR)
	 * indicates that a CILP ctxsw save has finished
	 */
	if ((gr_fecs_intr &
		gr_fecs_host_int_status_ctxsw_intr_f(CTXSW_INTR1)) != 0U) {
		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg | gpu_dbg_intr,
				"CILP: ctxsw save completed!\n");

		/* now clear the interrupt */
		gk20a_writel(g, gr_fecs_host_int_clear_r(),
				gr_fecs_host_int_clear_ctxsw_intr1_clear_f());

		ret = gr_gp10b_get_cilp_preempt_pending_chid(g, &chid);
		if ((ret != 0) || (chid == FIFO_INVAL_CHANNEL_ID)) {
			goto clean_up;
		}

		ch = gk20a_channel_from_id(g, chid);
		if (ch == NULL) {
			goto clean_up;
		}


		/* set preempt_pending to false */
		ret = gr_gp10b_clear_cilp_preempt_pending(g, ch);
		if (ret != 0) {
			nvgpu_err(g, "CILP: error while unsetting CILP preempt pending!");
			gk20a_channel_put(ch);
			goto clean_up;
		}

#ifdef NVGPU_DEBUGGER
		/* Post events to UMD */
		g->ops.debugger.post_events(ch);
#endif

		tsg = &g->fifo.tsg[ch->tsgid];

		g->ops.fifo.post_event_id(tsg,
					NVGPU_EVENT_ID_CILP_PREEMPTION_COMPLETE);

		gk20a_channel_put(ch);
	}

clean_up:
	/* handle any remaining interrupts */
	return gk20a_gr_handle_fecs_error(g, __ch, isr_data);
}

u32 gp10b_gr_get_sm_hww_warp_esr(struct gk20a *g,
			u32 gpc, u32 tpc, u32 sm)
{
	u32 offset = gk20a_gr_gpc_offset(g, gpc) + gk20a_gr_tpc_offset(g, tpc);
	u32 hww_warp_esr = gk20a_readl(g,
			 gr_gpc0_tpc0_sm_hww_warp_esr_r() + offset);

	if ((hww_warp_esr & gr_gpc0_tpc0_sm_hww_warp_esr_addr_valid_m()) == 0U) {
		hww_warp_esr = set_field(hww_warp_esr,
			gr_gpc0_tpc0_sm_hww_warp_esr_addr_error_type_m(),
			gr_gpc0_tpc0_sm_hww_warp_esr_addr_error_type_none_f());
	}

	return hww_warp_esr;
}

u32 get_ecc_override_val(struct gk20a *g)
{
	bool en = false;

	if (g->ops.fuse.is_opt_ecc_enable != NULL) {
		en = g->ops.fuse.is_opt_ecc_enable(g);
		if (en) {
			return gk20a_readl(g, gr_fecs_feature_override_ecc_r());
		}
	}

	return 0;
}

bool gr_gp10b_suspend_context(struct channel_gk20a *ch,
				bool *cilp_preempt_pending)
{
	struct gk20a *g = ch->g;
	struct tsg_gk20a *tsg;
	struct nvgpu_gr_ctx *gr_ctx;
	bool ctx_resident = false;
	int err = 0;

	tsg = tsg_gk20a_from_ch(ch);
	if (tsg == NULL) {
		return -EINVAL;
	}

	gr_ctx = tsg->gr_ctx;

	*cilp_preempt_pending = false;

	if (gk20a_is_channel_ctx_resident(ch)) {
		g->ops.gr.suspend_all_sms(g, 0, false);

		if (gr_ctx->compute_preempt_mode == NVGPU_PREEMPTION_MODE_COMPUTE_CILP) {
			err = gr_gp10b_set_cilp_preempt_pending(g, ch);
			if (err != 0) {
				nvgpu_err(g, "unable to set CILP preempt pending");
			} else {
				*cilp_preempt_pending = true;
			}

			g->ops.gr.resume_all_sms(g);
		}

		ctx_resident = true;
	} else {
		gk20a_disable_channel_tsg(g, ch);
	}

	return ctx_resident;
}

int gr_gp10b_suspend_contexts(struct gk20a *g,
				struct dbg_session_gk20a *dbg_s,
				int *ctx_resident_ch_fd)
{
	u32 delay = GR_IDLE_CHECK_DEFAULT;
	bool cilp_preempt_pending = false;
	struct channel_gk20a *cilp_preempt_pending_ch = NULL;
	struct channel_gk20a *ch;
	struct dbg_session_channel_data *ch_data;
	int err = 0;
	int local_ctx_resident_ch_fd = -1;
	bool ctx_resident;

	nvgpu_mutex_acquire(&g->dbg_sessions_lock);

	err = gr_gk20a_disable_ctxsw(g);
	if (err != 0) {
		nvgpu_err(g, "unable to stop gr ctxsw");
		nvgpu_mutex_release(&g->dbg_sessions_lock);
		goto clean_up;
	}

	nvgpu_mutex_acquire(&dbg_s->ch_list_lock);

	nvgpu_list_for_each_entry(ch_data, &dbg_s->ch_list,
			dbg_session_channel_data, ch_entry) {
		ch = g->fifo.channel + ch_data->chid;

		ctx_resident = gr_gp10b_suspend_context(ch,
					&cilp_preempt_pending);
		if (ctx_resident) {
			local_ctx_resident_ch_fd = ch_data->channel_fd;
		}
		if (cilp_preempt_pending) {
			cilp_preempt_pending_ch = ch;
		}
	}

	nvgpu_mutex_release(&dbg_s->ch_list_lock);

	err = gr_gk20a_enable_ctxsw(g);
	if (err != 0) {
		nvgpu_mutex_release(&g->dbg_sessions_lock);
		goto clean_up;
	}

	nvgpu_mutex_release(&g->dbg_sessions_lock);

	if (cilp_preempt_pending_ch != NULL) {
		struct tsg_gk20a *tsg;
		struct nvgpu_gr_ctx *gr_ctx;
		struct nvgpu_timeout timeout;

		nvgpu_log(g, gpu_dbg_fn | gpu_dbg_gpu_dbg | gpu_dbg_intr,
			"CILP preempt pending, waiting %u msecs for preemption",
			gk20a_get_gr_idle_timeout(g));

		tsg = tsg_gk20a_from_ch(cilp_preempt_pending_ch);
		if (tsg == NULL) {
			err = -EINVAL;
			goto clean_up;
		}

		gr_ctx = tsg->gr_ctx;

		nvgpu_timeout_init(g, &timeout, gk20a_get_gr_idle_timeout(g),
				   NVGPU_TIMER_CPU_TIMER);
		do {
			if (!gr_ctx->cilp_preempt_pending) {
				break;
			}

			nvgpu_usleep_range(delay, delay * 2U);
			delay = min_t(u32, delay << 1, GR_IDLE_CHECK_MAX);
		} while (nvgpu_timeout_expired(&timeout) == 0);

		/* If cilp is still pending at this point, timeout */
		if (gr_ctx->cilp_preempt_pending) {
			err = -ETIMEDOUT;
		}
	}

	*ctx_resident_ch_fd = local_ctx_resident_ch_fd;

clean_up:
	return err;
}

int gr_gp10b_set_boosted_ctx(struct channel_gk20a *ch,
				    bool boost)
{
	struct tsg_gk20a *tsg;
	struct nvgpu_gr_ctx *gr_ctx;
	struct gk20a *g = ch->g;
	struct nvgpu_mem *mem;
	int err = 0;

	tsg = tsg_gk20a_from_ch(ch);
	if (tsg == NULL) {
		return -EINVAL;
	}

	gr_ctx = tsg->gr_ctx;
	gr_ctx->boosted_ctx = boost;
	mem = &gr_ctx->mem;

	err = gk20a_disable_channel_tsg(g, ch);
	if (err != 0) {
		return err;
	}

	err = gk20a_fifo_preempt(g, ch);
	if (err != 0) {
		goto enable_ch;
	}

	if (g->ops.gr.ctxsw_prog.set_pmu_options_boost_clock_frequencies !=
			NULL) {
		g->ops.gr.ctxsw_prog.set_pmu_options_boost_clock_frequencies(g,
			mem, gr_ctx->boosted_ctx);
	} else {
		err = -ENOSYS;
	}

enable_ch:
	gk20a_enable_channel_tsg(g, ch);

	return err;
}

int gr_gp10b_set_preemption_mode(struct channel_gk20a *ch,
					u32 graphics_preempt_mode,
					u32 compute_preempt_mode)
{
	struct nvgpu_gr_ctx *gr_ctx;
	struct gk20a *g = ch->g;
	struct tsg_gk20a *tsg;
	struct vm_gk20a *vm;
	u32 class;
	int err = 0;

	class = ch->obj_class;
	if (class == 0U) {
		return -EINVAL;
	}

	tsg = tsg_gk20a_from_ch(ch);
	if (tsg == NULL) {
		return -EINVAL;
	}

	vm = tsg->vm;
	gr_ctx = tsg->gr_ctx;

	/* skip setting anything if both modes are already set */
	if ((graphics_preempt_mode != 0U) &&
	    (graphics_preempt_mode == gr_ctx->graphics_preempt_mode)) {
		graphics_preempt_mode = 0;
	}

	if ((compute_preempt_mode != 0U) &&
	    (compute_preempt_mode == gr_ctx->compute_preempt_mode)) {
		compute_preempt_mode = 0;
	}

	if ((graphics_preempt_mode == 0U) && (compute_preempt_mode == 0U)) {
		return 0;
	}

	if (g->ops.gr.set_ctxsw_preemption_mode != NULL) {

		nvgpu_log(g, gpu_dbg_sched, "chid=%d tsgid=%d pid=%d "
				"graphics_preempt=%d compute_preempt=%d",
				ch->chid,
				ch->tsgid,
				ch->tgid,
				graphics_preempt_mode,
				compute_preempt_mode);
		err = g->ops.gr.set_ctxsw_preemption_mode(g, gr_ctx, vm, class,
						graphics_preempt_mode, compute_preempt_mode);
		if (err != 0) {
			nvgpu_err(g, "set_ctxsw_preemption_mode failed");
			return err;
		}
	}

	err = gk20a_disable_channel_tsg(g, ch);
	if (err != 0) {
		return err;
	}

	err = gk20a_fifo_preempt(g, ch);
	if (err != 0) {
		goto enable_ch;
	}

	if (g->ops.gr.update_ctxsw_preemption_mode != NULL) {
		g->ops.gr.update_ctxsw_preemption_mode(ch->g, gr_ctx,
				ch->subctx);

		err = nvgpu_gr_ctx_patch_write_begin(g, gr_ctx, true);
		if (err != 0) {
			nvgpu_err(g, "can't map patch context");
			goto enable_ch;
		}
		g->ops.gr.commit_global_cb_manager(g, gr_ctx, true);
		nvgpu_gr_ctx_patch_write_end(g, gr_ctx, true);
	}

enable_ch:
	gk20a_enable_channel_tsg(g, ch);
	return err;
}

int gr_gp10b_get_preemption_mode_flags(struct gk20a *g,
	struct nvgpu_preemption_modes_rec *preemption_modes_rec)
{
	preemption_modes_rec->graphics_preemption_mode_flags = (
			NVGPU_PREEMPTION_MODE_GRAPHICS_WFI |
			NVGPU_PREEMPTION_MODE_GRAPHICS_GFXP);
	preemption_modes_rec->compute_preemption_mode_flags = (
			NVGPU_PREEMPTION_MODE_COMPUTE_WFI |
			NVGPU_PREEMPTION_MODE_COMPUTE_CTA |
			NVGPU_PREEMPTION_MODE_COMPUTE_CILP);

	preemption_modes_rec->default_graphics_preempt_mode =
			NVGPU_PREEMPTION_MODE_GRAPHICS_WFI;
	preemption_modes_rec->default_compute_preempt_mode =
			NVGPU_PREEMPTION_MODE_COMPUTE_WFI;

	return 0;
}

int gr_gp10b_init_preemption_state(struct gk20a *g)
{
	u32 debug_2;
	struct gr_gk20a *gr = &g->gr;
	u32 sysclk_cycles = gr->gfxp_wfi_timeout_count;
	gk20a_writel(g, gr_fe_gfxp_wfi_timeout_r(),
			gr_fe_gfxp_wfi_timeout_count_f(sysclk_cycles));

	debug_2 = gk20a_readl(g, gr_debug_2_r());
	debug_2 = set_field(debug_2,
			gr_debug_2_gfxp_wfi_always_injects_wfi_m(),
			gr_debug_2_gfxp_wfi_always_injects_wfi_enabled_f());
	gk20a_writel(g, gr_debug_2_r(), debug_2);

	return 0;
}

void gr_gp10b_set_preemption_buffer_va(struct gk20a *g,
			struct nvgpu_mem *mem, u64 gpu_va)
{
	g->ops.gr.ctxsw_prog.set_full_preemption_ptr(g, mem, gpu_va);
}

void gr_gp10b_init_czf_bypass(struct gk20a *g)
{
	g->gr.czf_bypass = gr_gpc0_prop_debug1_czf_bypass_init_v();
}

int gr_gp10b_set_czf_bypass(struct gk20a *g, struct channel_gk20a *ch)
{
	struct nvgpu_dbg_reg_op ops;

	ops.op     = REGOP(WRITE_32);
	ops.type   = REGOP(TYPE_GR_CTX);
	ops.status = REGOP(STATUS_SUCCESS);
	ops.value_hi      = 0;
	ops.and_n_mask_lo = gr_gpc0_prop_debug1_czf_bypass_m();
	ops.and_n_mask_hi = 0;
	ops.offset   = gr_gpc0_prop_debug1_r();
	ops.value_lo = gr_gpc0_prop_debug1_czf_bypass_f(
		g->gr.czf_bypass);

	return __gr_gk20a_exec_ctx_ops(ch, &ops, 1, 1, 0, false);
}

void gr_gp10b_init_gfxp_wfi_timeout_count(struct gk20a *g)
{
	struct gr_gk20a *gr = &g->gr;

	gr->gfxp_wfi_timeout_count = GFXP_WFI_TIMEOUT_COUNT_DEFAULT;
}

unsigned long gr_gp10b_get_max_gfxp_wfi_timeout_count(struct gk20a *g)
{
	/* 100msec @ 1GHZ */
	return (100UL * 1000UL * 1000UL);
}

u32 gp10b_gr_get_ctx_spill_size(struct gk20a *g) {
	return  gr_gpc0_swdx_rm_spill_buffer_size_256b_default_v() *
		gr_gpc0_swdx_rm_spill_buffer_size_256b_byte_granularity_v();
}

u32 gp10b_gr_get_ctx_pagepool_size(struct gk20a *g) {
	return g->ops.gr.pagepool_default_size(g) *
		gr_scc_pagepool_total_pages_byte_granularity_v();
}

u32 gp10b_gr_get_ctx_betacb_size(struct gk20a *g) {
	return g->gr.attrib_cb_default_size +
		(gr_gpc0_ppc0_cbm_beta_cb_size_v_gfxp_v() -
		 gr_gpc0_ppc0_cbm_beta_cb_size_v_default_v());
}

u32 gp10b_gr_get_ctx_attrib_cb_size(struct gk20a *g, u32 betacb_size) {
	return (betacb_size + g->gr.alpha_cb_size) *
		gr_gpc0_ppc0_cbm_beta_cb_size_v_granularity_v() *
		nvgpu_gr_config_get_max_tpc_count(g->gr.config);
}
