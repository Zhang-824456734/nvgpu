/*
 * Copyright (c) 2019, NVIDIA CORPORATION.  All rights reserved.
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

#include <nvgpu/gk20a.h>
#include <nvgpu/list.h>
#include <nvgpu/log.h>
#include <nvgpu/gr/fecs_trace.h>

int nvgpu_gr_fecs_trace_add_context(struct gk20a *g, u32 context_ptr,
	pid_t pid, u32 vmid, struct nvgpu_list_node *list)
{
	struct nvgpu_fecs_trace_context_entry *entry;

	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_ctxsw,
		"adding hash entry context_ptr=%x -> pid=%d, vmid=%d",
		context_ptr, pid, vmid);

	entry = nvgpu_kzalloc(g, sizeof(*entry));
	if (unlikely(!entry)) {
		nvgpu_err(g,
			"can't alloc new entry for context_ptr=%x pid=%d vmid=%d",
			context_ptr, pid, vmid);
		return -ENOMEM;
	}

	nvgpu_init_list_node(&entry->entry);
	entry->context_ptr = context_ptr;
	entry->pid = pid;
	entry->vmid = vmid;

	nvgpu_list_add_tail(&entry->entry, list);

	return 0;
}

void nvgpu_gr_fecs_trace_remove_context(struct gk20a *g, u32 context_ptr,
	struct nvgpu_list_node *list)
{
	struct nvgpu_fecs_trace_context_entry *entry, *tmp;

	nvgpu_log(g, gpu_dbg_fn | gpu_dbg_ctxsw,
		"freeing entry context_ptr=%x", context_ptr);

	nvgpu_list_for_each_entry_safe(entry, tmp, list,
			nvgpu_fecs_trace_context_entry,	entry) {
		if (entry->context_ptr == context_ptr) {
			nvgpu_list_del(&entry->entry);
			nvgpu_log(g, gpu_dbg_ctxsw,
				"freed entry=%p context_ptr=%x", entry,
				entry->context_ptr);
			nvgpu_kfree(g, entry);
			break;
		}
	}
}

void nvgpu_gr_fecs_trace_remove_contexts(struct gk20a *g,
	struct nvgpu_list_node *list)
{
	struct nvgpu_fecs_trace_context_entry *entry, *tmp;

	nvgpu_list_for_each_entry_safe(entry, tmp, list,
			nvgpu_fecs_trace_context_entry,	entry) {
		nvgpu_list_del(&entry->entry);
		nvgpu_kfree(g, entry);
	}
}

void nvgpu_gr_fecs_trace_find_pid(struct gk20a *g, u32 context_ptr,
	struct nvgpu_list_node *list, pid_t *pid, u32 *vmid)
{
	struct nvgpu_fecs_trace_context_entry *entry;

	nvgpu_list_for_each_entry(entry, list, nvgpu_fecs_trace_context_entry,
			entry) {
		if (entry->context_ptr == context_ptr) {
			nvgpu_log(g, gpu_dbg_ctxsw,
				"found context_ptr=%x -> pid=%d, vmid=%d",
				entry->context_ptr, entry->pid, entry->vmid);
			*pid = entry->pid;
			*vmid = entry->vmid;
			return;
		}
	}

	*pid = 0;
	*vmid = 0xffffffffU;
}
