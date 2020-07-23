/*
 * Copyright (c) 2020, NVIDIA CORPORATION.  All rights reserved.
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

#ifndef NVGPU_PROFILER_H
#define NVGPU_PROFILER_H

#ifdef CONFIG_NVGPU_PROFILER

#include <nvgpu/list.h>
#include <nvgpu/lock.h>
#include <nvgpu/pm_reservation.h>

struct gk20a;
struct nvgpu_channel;
struct nvgpu_tsg;

struct nvgpu_profiler_object {
	struct gk20a *g;

	/*
	 * Debug session id. Only valid for profiler objects
	 * allocated through debug session i.e. in ioctl_dbg.c.
	 */
	int session_id;

	/* Unique profiler object handle. Also used as reservation id. */
	u32 prof_handle;

	/*
	 * Pointer to context being profiled, applicable only for profiler
	 * objects with context scope.
	 */
	struct nvgpu_tsg *tsg;

	/*
	 * If context has been bound by userspace.
	 * For objects with device scope, userspace should still trigger
	 * BIND_CONTEXT IOCTL/DEVCTL with tsg_fd = -1 for consistency.
	 */
	bool context_init;

	/* Lock to serialize IOCTL/DEVCTL calls */
	struct nvgpu_mutex ioctl_lock;

	/* If profiler object has reservation for each resource. */
	bool reserved[NVGPU_PROFILER_PM_RESOURCE_TYPE_COUNT];

	/* Scope of the profiler object */
	enum nvgpu_profiler_pm_reservation_scope scope;

	/*
	 * Entry of this object into global list of objects
	 * maintained in struct gk20a.
	 */
	struct nvgpu_list_node prof_obj_entry;
};

static inline struct nvgpu_profiler_object *
nvgpu_profiler_object_from_prof_obj_entry(struct nvgpu_list_node *node)
{
	return (struct nvgpu_profiler_object *)
	((uintptr_t)node - offsetof(struct nvgpu_profiler_object, prof_obj_entry));
};

int nvgpu_profiler_alloc(struct gk20a *g,
	struct nvgpu_profiler_object **_prof,
	enum nvgpu_profiler_pm_reservation_scope scope);
void nvgpu_profiler_free(struct nvgpu_profiler_object *prof);

int nvgpu_profiler_bind_context(struct nvgpu_profiler_object *prof,
	struct nvgpu_tsg *tsg);
int nvgpu_profiler_unbind_context(struct nvgpu_profiler_object *prof);

int nvgpu_profiler_pm_resource_reserve(struct nvgpu_profiler_object *prof,
	enum nvgpu_profiler_pm_resource_type pm_resource);
int nvgpu_profiler_pm_resource_release(struct nvgpu_profiler_object *prof,
	enum nvgpu_profiler_pm_resource_type pm_resource);

#endif /* CONFIG_NVGPU_PROFILER */
#endif /* NVGPU_PROFILER_H */