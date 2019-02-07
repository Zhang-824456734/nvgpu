/*
 * Copyright (c) 2017-2019, NVIDIA CORPORATION.  All rights reserved.
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

#include <nvgpu/lock.h>
#include <nvgpu/pmu.h>

#include "falcon_queue_priv.h"
#include "falcon_priv.h"
#include "falcon_dmem_queue.h"
#include "falcon_emem_queue.h"
#include "falcon_fb_queue.h"

/* common falcon queue ops */
static int falcon_queue_head(struct nvgpu_falcon *flcn,
	struct nvgpu_falcon_queue *queue, u32 *head, bool set)
{
	int err = -ENOSYS;

	if (flcn->flcn_engine_dep_ops.queue_head != NULL) {
		err = flcn->flcn_engine_dep_ops.queue_head(flcn->g, queue->id,
			queue->index, head, set);
	}

	return err;
}

static int falcon_queue_tail(struct nvgpu_falcon *flcn,
	struct nvgpu_falcon_queue *queue, u32 *tail, bool set)
{
	int err = -ENOSYS;

	if (flcn->flcn_engine_dep_ops.queue_tail != NULL) {
		err = flcn->flcn_engine_dep_ops.queue_tail(flcn->g, queue->id,
			queue->index, tail, set);
	}

	return err;
}

static bool falcon_queue_has_room(struct nvgpu_falcon *flcn,
	struct nvgpu_falcon_queue *queue, u32 size, bool *need_rewind)
{
	u32 q_head = 0;
	u32 q_tail = 0;
	u32 q_free = 0;
	bool q_rewind = false;
	int err = 0;

	size = ALIGN(size, QUEUE_ALIGNMENT);

	err = queue->head(flcn, queue, &q_head, QUEUE_GET);
	if (err != 0) {
		nvgpu_err(flcn->g, "queue head GET failed");
		goto exit;
	}

	err = queue->tail(flcn, queue, &q_tail, QUEUE_GET);
	if (err != 0) {
		nvgpu_err(flcn->g, "queue tail GET failed");
		goto exit;
	}

	if (q_head >= q_tail) {
		q_free = queue->offset + queue->size - q_head;
		q_free -= (u32)PMU_CMD_HDR_SIZE;

		if (size > q_free) {
			q_rewind = true;
			q_head = queue->offset;
		}
	}

	if (q_head < q_tail) {
		q_free = q_tail - q_head - 1U;
	}

	if (need_rewind != NULL) {
		*need_rewind = q_rewind;
	}

exit:
	return size <= q_free;
}

static int falcon_queue_rewind(struct nvgpu_falcon *flcn,
	struct nvgpu_falcon_queue *queue)
{
	struct gk20a *g = flcn->g;
	struct pmu_cmd cmd;
	int err = 0;

	if (queue->oflag == OFLAG_WRITE) {
		cmd.hdr.unit_id = PMU_UNIT_REWIND;
		cmd.hdr.size = (u8)PMU_CMD_HDR_SIZE;
		err = queue->push(flcn, queue, &cmd, cmd.hdr.size);
		if (err != 0) {
			nvgpu_err(g, "flcn-%d queue-%d, rewind request failed",
				flcn->flcn_id, queue->id);
			goto exit;
		} else {
			nvgpu_pmu_dbg(g, "flcn-%d queue-%d, rewinded",
			flcn->flcn_id, queue->id);
		}
	}

	/* update queue position */
	queue->position = queue->offset;

	if (queue->oflag == OFLAG_READ) {
		err = queue->tail(flcn, queue, &queue->position,
			QUEUE_SET);
		if (err != 0){
			nvgpu_err(flcn->g, "flcn-%d queue-%d, position SET failed",
				flcn->flcn_id, queue->id);
			goto exit;
		}
	}

exit:
	return err;
}

static int falcon_queue_prepare_write(struct nvgpu_falcon *flcn,
	struct nvgpu_falcon_queue *queue, u32 size)
{
	bool q_rewind = false;
	int err = 0;

	/* make sure there's enough free space for the write */
	if (!queue->has_room(flcn, queue, size, &q_rewind)) {
		nvgpu_pmu_dbg(flcn->g, "queue full: queue-id %d: index %d",
			queue->id, queue->index);
		err = -EAGAIN;
		goto exit;
	}

	err = queue->head(flcn, queue, &queue->position, QUEUE_GET);
	if (err != 0) {
		nvgpu_err(flcn->g, "flcn-%d queue-%d, position GET failed",
			flcn->flcn_id, queue->id);
		goto exit;
	}

	if (q_rewind) {
		if (queue->rewind != NULL)  {
			err = queue->rewind(flcn, queue);
		}
	}

exit:
	return err;
}

/* queue public functions */

/* queue push operation with lock */
int nvgpu_falcon_queue_push(struct nvgpu_falcon *flcn,
	struct nvgpu_falcon_queue *queue, void *data, u32 size)
{
	int err = 0;

	if ((flcn == NULL) || (queue == NULL)) {
		return -EINVAL;
	}

	if (queue->oflag != OFLAG_WRITE) {
		nvgpu_err(flcn->g, "flcn-%d, queue-%d not opened for write",
			flcn->flcn_id, queue->id);
		err = -EINVAL;
		goto exit;
	}

	/* acquire mutex */
	nvgpu_mutex_acquire(&queue->mutex);

	err = falcon_queue_prepare_write(flcn, queue, size);
	if (err != 0) {
		goto unlock_mutex;
	}

	err = queue->push(flcn, queue, data, size);
	if (err != 0) {
		nvgpu_err(flcn->g, "flcn-%d queue-%d, fail to write",
			flcn->flcn_id, queue->id);
	}

	err = queue->head(flcn, queue, &queue->position, QUEUE_SET);
	if (err != 0){
		nvgpu_err(flcn->g, "flcn-%d queue-%d, position SET failed",
			flcn->flcn_id, queue->id);
	}

unlock_mutex:
	/* release mutex */
	nvgpu_mutex_release(&queue->mutex);
exit:
	return err;
}

/* queue pop operation with lock */
int nvgpu_falcon_queue_pop(struct nvgpu_falcon *flcn,
	struct nvgpu_falcon_queue *queue, void *data, u32 size,
	u32 *bytes_read)
{
	int err = 0;

	if ((flcn == NULL) || (queue == NULL)) {
		return -EINVAL;
	}

	if (queue->oflag != OFLAG_READ) {
		nvgpu_err(flcn->g, "flcn-%d, queue-%d, not opened for read",
			flcn->flcn_id, queue->id);
		err = -EINVAL;
		goto exit;
	}

	/* acquire mutex */
	nvgpu_mutex_acquire(&queue->mutex);

	err = queue->tail(flcn, queue, &queue->position, QUEUE_GET);
	if (err != 0) {
		nvgpu_err(flcn->g, "flcn-%d queue-%d, position GET failed",
			flcn->flcn_id, queue->id);
		goto unlock_mutex;
	}

	err = queue->pop(flcn, queue, data, size, bytes_read);
	if (err != 0) {
		nvgpu_err(flcn->g, "flcn-%d queue-%d, fail to read",
			flcn->flcn_id, queue->id);
	}

	err = queue->tail(flcn, queue, &queue->position, QUEUE_SET);
	if (err != 0){
		nvgpu_err(flcn->g, "flcn-%d queue-%d, position SET failed",
			flcn->flcn_id, queue->id);
	}

unlock_mutex:
	/* release mutex */
	nvgpu_mutex_release(&queue->mutex);
exit:
	return err;
}

int nvgpu_falcon_queue_rewind(struct nvgpu_falcon *flcn,
	struct nvgpu_falcon_queue *queue)
{
	int err = 0;

	if ((flcn == NULL) || (queue == NULL)) {
		return -EINVAL;
	}

	/* acquire mutex */
	nvgpu_mutex_acquire(&queue->mutex);

	if (queue->rewind != NULL) {
		err = queue->rewind(flcn, queue);
	}

	/* release mutex */
	nvgpu_mutex_release(&queue->mutex);

	return err;
}

/* queue is_empty check with lock */
bool nvgpu_falcon_queue_is_empty(struct nvgpu_falcon *flcn,
	struct nvgpu_falcon_queue *queue)
{
	u32 q_head = 0;
	u32 q_tail = 0;
	int err = 0;

	if ((flcn == NULL) || (queue == NULL)) {
		return true;
	}

	/* acquire mutex */
	nvgpu_mutex_acquire(&queue->mutex);

	err = queue->head(flcn, queue, &q_head, QUEUE_GET);
	if (err != 0) {
		nvgpu_err(flcn->g, "flcn-%d queue-%d, head GET failed",
			flcn->flcn_id, queue->id);
		goto exit;
	}

	err = queue->tail(flcn, queue, &q_tail, QUEUE_GET);
	if (err != 0) {
		nvgpu_err(flcn->g, "flcn-%d queue-%d, tail GET failed",
			flcn->flcn_id, queue->id);
		goto exit;
	}

exit:
	/* release mutex */
	nvgpu_mutex_release(&queue->mutex);

	return q_head == q_tail;
}

void nvgpu_falcon_queue_free(struct nvgpu_falcon *flcn,
	struct nvgpu_falcon_queue **queue_p)
{
	struct nvgpu_falcon_queue *queue = NULL;
	struct gk20a *g = flcn->g;

	if ((queue_p == NULL) || (*queue_p == NULL)) {
		return;
	}

	queue = *queue_p;

	nvgpu_pmu_dbg(g, "flcn id-%d q-id %d: index %d ",
		      flcn->flcn_id, queue->id, queue->index);

	if (queue->queue_type == QUEUE_TYPE_FB) {
		nvgpu_kfree(g, queue->fbq.work_buffer);
		nvgpu_mutex_destroy(&queue->fbq.work_buffer_mutex);
	}

	/* destroy mutex */
	nvgpu_mutex_destroy(&queue->mutex);

	nvgpu_kfree(g, queue);
	*queue_p = NULL;
}

u32 nvgpu_falcon_queue_get_id(struct nvgpu_falcon_queue *queue)
{
	return queue->id;
}

u32 nvgpu_falcon_queue_get_position(struct nvgpu_falcon_queue *queue)
{
	return queue->position;
}

u32 nvgpu_falcon_queue_get_index(struct nvgpu_falcon_queue *queue)
{
	return queue->index;
}

u32 nvgpu_falcon_queue_get_size(struct nvgpu_falcon_queue *queue)
{
	return queue->size;
}

u32 nvgpu_falcon_fbq_get_element_size(struct nvgpu_falcon_queue *queue)
{
	return falcon_queue_get_element_size_fb(queue);
}

u32 nvgpu_falcon_queue_get_fbq_offset(struct nvgpu_falcon_queue *queue)
{
	return falcon_queue_get_offset_fb(queue);
}

void nvgpu_falcon_queue_lock_fbq_work_buffer(struct nvgpu_falcon_queue *queue)
{
	falcon_queue_lock_work_buffer_fb(queue);
}

void nvgpu_falcon_queue_unlock_fbq_work_buffer(struct nvgpu_falcon_queue *queue)
{
	falcon_queue_unlock_work_buffer_fb(queue);
}

u8* nvgpu_falcon_queue_get_fbq_work_buffer(struct nvgpu_falcon_queue *queue)
{
	return falcon_queue_get_work_buffer_fb(queue);
}

int nvgpu_falcon_queue_free_fbq_element(struct nvgpu_falcon *flcn,
	struct nvgpu_falcon_queue *queue, u32 queue_pos)
{
	return falcon_queue_free_element_fb(flcn, queue, queue_pos);
}

int nvgpu_falcon_queue_init(struct nvgpu_falcon *flcn,
	struct nvgpu_falcon_queue **queue_p,
	struct nvgpu_falcon_queue_params params)
{
	struct nvgpu_falcon_queue *queue = NULL;
	struct gk20a *g = flcn->g;
	int err = 0;

	if (queue_p == NULL) {
		return -EINVAL;
	}

	queue = (struct nvgpu_falcon_queue *)
		   nvgpu_kmalloc(g, sizeof(struct nvgpu_falcon_queue));

	if (queue == NULL) {
		return -ENOMEM;
	}

	queue->g = g;
	queue->id = params.id;
	queue->index = params.index;
	queue->offset = params.offset;
	queue->position = params.position;
	queue->size = params.size;
	queue->oflag = params.oflag;
	queue->queue_type = params.queue_type;

	queue->head = falcon_queue_head;
	queue->tail = falcon_queue_tail;
	queue->has_room = falcon_queue_has_room;
	queue->rewind = falcon_queue_rewind;

	nvgpu_log(g, gpu_dbg_pmu,
		"flcn id-%d q-id %d: index %d, offset 0x%08x, size 0x%08x",
		flcn->flcn_id, queue->id, queue->index,
		queue->offset, queue->size);

	switch (queue->queue_type) {
	case QUEUE_TYPE_DMEM:
		falcon_dmem_queue_init(flcn, queue);
		break;
	case QUEUE_TYPE_EMEM:
		falcon_emem_queue_init(flcn, queue);
		break;
	case QUEUE_TYPE_FB:
		queue->fbq.super_surface_mem = params.super_surface_mem;
		queue->fbq.element_size = params.fbq_element_size;
		queue->fbq.fb_offset = params.fbq_offset;

		err = falcon_fb_queue_init(flcn, queue);
		if (err != 0x0) {
			goto exit;
		}
		break;
	default:
		err = -EINVAL;
		break;
	}

	if (err != 0) {
		nvgpu_err(flcn->g, "flcn-%d queue-%d, init failed",
			flcn->flcn_id, queue->id);
		nvgpu_kfree(g, queue);
		goto exit;
	}

	/* init mutex */
	err = nvgpu_mutex_init(&queue->mutex);
	if (err != 0) {
		goto exit;
	}

	*queue_p = queue;
exit:
	return err;
}
