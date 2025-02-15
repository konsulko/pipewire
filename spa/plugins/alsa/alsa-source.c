/* Spa ALSA Source
 * Copyright (C) 2016 Wim Taymans <wim.taymans@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <stddef.h>

#include <alsa/asoundlib.h>

#include <spa/node/node.h>
#include <spa/utils/list.h>
#include <spa/param/audio/format.h>
#include <spa/pod/filter.h>

#define NAME "alsa-source"

#include "alsa-utils.h"

#define CHECK_PORT(this,d,p)    ((d) == SPA_DIRECTION_OUTPUT && (p) == 0)

static const char default_device[] = "hw:0";
static const uint32_t default_min_latency = 1024;

static void reset_props(struct props *props)
{
	strncpy(props->device, default_device, 64);
	props->min_latency = default_min_latency;
}

static int impl_node_enum_params(struct spa_node *node,
				 uint32_t id, uint32_t *index,
				 const struct spa_pod *filter,
				 struct spa_pod **result,
				 struct spa_pod_builder *builder)
{
	struct state *this;
	struct type *t;
	struct spa_pod *param;
	uint8_t buffer[1024];
	struct spa_pod_builder b = { 0 };
	struct props *p;


	spa_return_val_if_fail(node != NULL, -EINVAL);
	spa_return_val_if_fail(index != NULL, -EINVAL);
	spa_return_val_if_fail(builder != NULL, -EINVAL);

	this = SPA_CONTAINER_OF(node, struct state, node);
	t = &this->type;
	p = &this->props;

      next:
	spa_pod_builder_init(&b, buffer, sizeof(buffer));

	if (id == t->param.idList) {
		uint32_t list[] = { t->param.idPropInfo,
				    t->param.idProps };

		if (*index < SPA_N_ELEMENTS(list))
			param = spa_pod_builder_object(&b, id, t->param.List,
				":", t->param.listId, "I", list[*index]);
		else
			return 0;
	}
	else if (id == t->param.idPropInfo) {
		switch (*index) {
		case 0:
			param = spa_pod_builder_object(&b,
				id, t->param.PropInfo,
				":", t->param.propId, "I", t->prop_device,
				":", t->param.propName, "s", "The ALSA device",
				":", t->param.propType, "S", p->device, sizeof(p->device));
			break;
		case 1:
			param = spa_pod_builder_object(&b,
				id, t->param.PropInfo,
				":", t->param.propId, "I", t->prop_device_name,
				":", t->param.propName, "s", "The ALSA device name",
				":", t->param.propType, "S-r", p->device_name, sizeof(p->device_name));
			break;
		case 2:
			param = spa_pod_builder_object(&b,
				id, t->param.PropInfo,
				":", t->param.propId, "I", t->prop_card_name,
				":", t->param.propName, "s", "The ALSA card name",
				":", t->param.propType, "S-r", p->card_name, sizeof(p->card_name));
			break;
		case 3:
			param = spa_pod_builder_object(&b,
				id, t->param.PropInfo,
				":", t->param.propId, "I", t->prop_min_latency,
				":", t->param.propName, "s", "The minimum latency",
				":", t->param.propType, "ir", p->min_latency,
					SPA_POD_PROP_MIN_MAX(1, INT32_MAX));
			break;
		default:
			return 0;
		}
	}
	else if (id == t->param.idProps) {
		switch (*index) {
		case 0:
			param = spa_pod_builder_object(&b,
				id, t->props,
				":", t->prop_device,      "S",   p->device, sizeof(p->device),
				":", t->prop_device_name, "S-r", p->device_name, sizeof(p->device_name),
				":", t->prop_card_name,   "S-r", p->card_name, sizeof(p->card_name),
				":", t->prop_min_latency, "i",   p->min_latency);
			break;
		default:
			return 0;
		}
	}
	else
		return -ENOENT;

	(*index)++;

	if (spa_pod_filter(builder, result, param, filter) < 0)
		goto next;

	return 1;
}

static int impl_node_set_param(struct spa_node *node, uint32_t id, uint32_t flags,
			       const struct spa_pod *param)
{
	struct state *this;
	struct type *t;

	spa_return_val_if_fail(node != NULL, -EINVAL);

	this = SPA_CONTAINER_OF(node, struct state, node);
	t = &this->type;

	if (id == t->param.idProps) {
		struct props *p = &this->props;

		if (param == NULL) {
			reset_props(p);
			return 0;
		}
		spa_pod_object_parse(param,
			":", t->prop_device,      "?S", p->device, sizeof(p->device),
			":", t->prop_min_latency, "?i", &p->min_latency, NULL);
	}
	else
		return -ENOENT;

	return 0;
}

static int impl_node_send_command(struct spa_node *node, const struct spa_command *command)
{
	struct state *this;
	int res;

	spa_return_val_if_fail(node != NULL, -EINVAL);
	spa_return_val_if_fail(command != NULL, -EINVAL);

	this = SPA_CONTAINER_OF(node, struct state, node);

	if (SPA_COMMAND_TYPE(command) == this->type.command_node.Start) {
		if (!this->have_format)
			return -EIO;
		if (this->n_buffers == 0)
			return -EIO;

		if ((res = spa_alsa_start(this, false)) < 0)
			return res;
	} else if (SPA_COMMAND_TYPE(command) == this->type.command_node.Pause) {
		if ((res = spa_alsa_pause(this, false)) < 0)
			return res;
	} else
		return -ENOTSUP;

	return 0;
}

static int
impl_node_set_callbacks(struct spa_node *node,
			const struct spa_node_callbacks *callbacks,
			void *data)
{
	struct state *this;

	spa_return_val_if_fail(node != NULL, -EINVAL);

	this = SPA_CONTAINER_OF(node, struct state, node);

	this->callbacks = callbacks;
	this->callbacks_data = data;

	return 0;
}

static int
impl_node_get_n_ports(struct spa_node *node,
		      uint32_t *n_input_ports,
		      uint32_t *max_input_ports,
		      uint32_t *n_output_ports,
		      uint32_t *max_output_ports)
{
	spa_return_val_if_fail(node != NULL, -EINVAL);

	if (n_input_ports)
		*n_input_ports = 0;
	if (max_input_ports)
		*max_input_ports = 0;
	if (n_output_ports)
		*n_output_ports = 1;
	if (max_output_ports)
		*max_output_ports = 1;

	return 0;
}

static int
impl_node_get_port_ids(struct spa_node *node,
		       uint32_t *input_ids,
		       uint32_t n_input_ids,
		       uint32_t *output_ids,
		       uint32_t n_output_ids)
{
	spa_return_val_if_fail(node != NULL, -EINVAL);

	if (n_output_ids > 0 && output_ids != NULL)
		output_ids[0] = 0;

	return 0;
}


static int impl_node_add_port(struct spa_node *node, enum spa_direction direction, uint32_t port_id)
{
	return -ENOTSUP;
}

static int impl_node_remove_port(struct spa_node *node, enum spa_direction direction, uint32_t port_id)
{
	return -ENOTSUP;
}

static int
impl_node_port_get_info(struct spa_node *node,
			enum spa_direction direction, uint32_t port_id, const struct spa_port_info **info)
{
	struct state *this;

	spa_return_val_if_fail(node != NULL, -EINVAL);
	spa_return_val_if_fail(info != NULL, -EINVAL);

	this = SPA_CONTAINER_OF(node, struct state, node);

	spa_return_val_if_fail(CHECK_PORT(this, direction, port_id), -EINVAL);

	*info = &this->info;

	return 0;
}

static void recycle_buffer(struct state *this, uint32_t buffer_id)
{
	struct buffer *b;

	spa_log_trace(this->log, NAME " %p: recycle buffer %u", this, buffer_id);

	b = &this->buffers[buffer_id];
	spa_return_if_fail(b->outstanding);

	b->outstanding = false;
	spa_list_append(&this->free, &b->link);
}

static int port_get_format(struct spa_node *node,
			   enum spa_direction direction, uint32_t port_id,
			   uint32_t *index,
			   struct spa_pod **param,
			   struct spa_pod_builder *builder)
{
	struct state *this = SPA_CONTAINER_OF(node, struct state, node);
	struct type *t = &this->type;

	if (!this->have_format)
		return -EIO;
	if (*index > 0)
		return 0;

	*param = spa_pod_builder_object(builder,
		t->param.idFormat, t->format,
		"I", t->media_type.audio,
		"I", t->media_subtype.raw,
		":", t->format_audio.format,   "I", this->current_format.info.raw.format,
		":", t->format_audio.rate,     "i", this->current_format.info.raw.rate,
		":", t->format_audio.channels, "i", this->current_format.info.raw.channels);

	return 1;
}

static int
impl_node_port_enum_params(struct spa_node *node,
			   enum spa_direction direction, uint32_t port_id,
			   uint32_t id, uint32_t *index,
			   const struct spa_pod *filter,
			   struct spa_pod **result,
			   struct spa_pod_builder *builder)
{
	struct state *this;
	struct type *t;
	struct spa_pod *param;
	struct spa_pod_builder b = { 0 };
	uint8_t buffer[1024];
	int res;

	spa_return_val_if_fail(node != NULL, -EINVAL);
	spa_return_val_if_fail(index != NULL, -EINVAL);
	spa_return_val_if_fail(builder != NULL, -EINVAL);

	this = SPA_CONTAINER_OF(node, struct state, node);
	t = &this->type;

	spa_return_val_if_fail(CHECK_PORT(this, direction, port_id), -EINVAL);

      next:
	spa_pod_builder_init(&b, buffer, sizeof(buffer));

	if (id == t->param.idList) {
		uint32_t list[] = { t->param.idEnumFormat,
				    t->param.idFormat,
				    t->param.idBuffers,
				    t->param.idMeta };

		if (*index < SPA_N_ELEMENTS(list))
			param = spa_pod_builder_object(&b, id, t->param.List,
				":", t->param.listId, "I", list[*index]);
		else
			return 0;
	}
	else if (id == t->param.idEnumFormat) {
		return spa_alsa_enum_format(this, index, filter, result, builder);
	}
	else if (id == t->param.idFormat) {
		if ((res = port_get_format(node, direction, port_id, index, &param, &b)) <= 0)
			return res;
	}
	else if (id == t->param.idBuffers) {
		if (!this->have_format)
			return -EIO;
		if (*index > 0)
			return 0;

		param = spa_pod_builder_object(&b,
			id, t->param_buffers.Buffers,
			":", t->param_buffers.size,    "i", this->props.min_latency * this->frame_size,
			":", t->param_buffers.stride,  "i", 0,
			":", t->param_buffers.buffers, "ir", 2,
				SPA_POD_PROP_MIN_MAX(1, MAX_BUFFERS),
			":", t->param_buffers.align,   "i", 16);
	}
	else if (id == t->param.idMeta) {
		if (!this->have_format)
			return -EIO;

		switch (*index) {
		case 0:
			param = spa_pod_builder_object(&b,
				id, t->param_meta.Meta,
				":", t->param_meta.type, "I", t->meta.Header,
				":", t->param_meta.size, "i", sizeof(struct spa_meta_header));
			break;
		default:
			return 0;
		}
	}
	else
		return -ENOENT;

	(*index)++;

	if (spa_pod_filter(builder, result, param, filter) < 0)
		goto next;

	return 1;
}

static int clear_buffers(struct state *this)
{
	if (this->n_buffers > 0) {
		spa_list_init(&this->free);
		spa_list_init(&this->ready);
		this->n_buffers = 0;
	}
	return 0;
}

static int port_set_format(struct spa_node *node,
			   enum spa_direction direction, uint32_t port_id,
			   uint32_t flags, const struct spa_pod *format)
{
	struct state *this = SPA_CONTAINER_OF(node, struct state, node);
	int err;

	if (format == NULL) {
		spa_alsa_pause(this, false);
		clear_buffers(this);
		spa_alsa_close(this);
		this->have_format = false;
	} else {
		struct spa_audio_info info = { 0 };

		spa_pod_object_parse(format,
			"I", &info.media_type,
			"I", &info.media_subtype);

		if (info.media_type != this->type.media_type.audio ||
		    info.media_subtype != this->type.media_subtype.raw)
			return -EINVAL;

		if (spa_format_audio_raw_parse(format, &info.info.raw, &this->type.format_audio) < 0)
			return -EINVAL;

		if ((err = spa_alsa_set_format(this, &info, flags)) < 0)
			return err;

		this->current_format = info;
		this->have_format = true;
	}

	if (this->have_format) {
		this->info.rate = this->rate;
	}

	return 0;
}

static int
impl_node_port_set_param(struct spa_node *node,
			 enum spa_direction direction, uint32_t port_id,
			 uint32_t id, uint32_t flags,
			 const struct spa_pod *param)
{
	struct state *this;
	struct type *t;

	spa_return_val_if_fail(node != NULL, -EINVAL);

	this = SPA_CONTAINER_OF(node, struct state, node);
	t = &this->type;

	spa_return_val_if_fail(CHECK_PORT(this, direction, port_id), -EINVAL);

	if (id == t->param.idFormat) {
		return port_set_format(node, direction, port_id, flags, param);
	}
	else
		return -ENOENT;
}

static int
impl_node_port_use_buffers(struct spa_node *node,
			   enum spa_direction direction,
			   uint32_t port_id, struct spa_buffer **buffers, uint32_t n_buffers)
{
	struct state *this;
	int res;
	int i;

	spa_return_val_if_fail(node != NULL, -EINVAL);

	this = SPA_CONTAINER_OF(node, struct state, node);

	spa_return_val_if_fail(CHECK_PORT(this, direction, port_id), -EINVAL);

	if (!this->have_format)
		return -EIO;

	if (this->n_buffers > 0) {
		spa_alsa_pause(this, false);
		if ((res = clear_buffers(this)) < 0)
			return res;
	}
	for (i = 0; i < n_buffers; i++) {
		struct buffer *b = &this->buffers[i];
		struct spa_data *d = buffers[i]->datas;

		b->outbuf = buffers[i];
		b->outstanding = false;

		b->h = spa_buffer_find_meta(b->outbuf, this->type.meta.Header);

		if (!((d[0].type == this->type.data.MemFd ||
		       d[0].type == this->type.data.DmaBuf ||
		       d[0].type == this->type.data.MemPtr) && d[0].data != NULL)) {
			spa_log_error(this->log, NAME " %p: need mapped memory", this);
			return -EINVAL;
		}
		spa_list_append(&this->free, &b->link);
	}
	this->n_buffers = n_buffers;

	return 0;
}


static int
impl_node_port_alloc_buffers(struct spa_node *node,
			     enum spa_direction direction,
			     uint32_t port_id,
			     struct spa_pod **params,
			     uint32_t n_params,
			     struct spa_buffer **buffers,
			     uint32_t *n_buffers)
{
	struct state *this;

	spa_return_val_if_fail(node != NULL, -EINVAL);
	spa_return_val_if_fail(buffers != NULL, -EINVAL);

	this = SPA_CONTAINER_OF(node, struct state, node);

	spa_return_val_if_fail(CHECK_PORT(this, direction, port_id), -EINVAL);

	if (this->n_buffers == 0)
		return -EIO;

	return -ENOTSUP;
}

static int
impl_node_port_set_io(struct spa_node *node,
		      enum spa_direction direction,
		      uint32_t port_id,
		      uint32_t id,
		      void *data, size_t size)
{
	struct state *this;
	struct type *t;

	spa_return_val_if_fail(node != NULL, -EINVAL);

	this = SPA_CONTAINER_OF(node, struct state, node);
	t = &this->type;

	spa_return_val_if_fail(CHECK_PORT(this, direction, port_id), -EINVAL);

	if (id == t->io.Buffers)
		this->io = data;
	else
		return -ENOENT;

	return 0;
}

static int impl_node_port_reuse_buffer(struct spa_node *node, uint32_t port_id, uint32_t buffer_id)
{
	struct state *this;

	spa_return_val_if_fail(node != NULL, -EINVAL);

	this = SPA_CONTAINER_OF(node, struct state, node);

	spa_return_val_if_fail(port_id == 0, -EINVAL);

	if (this->n_buffers == 0)
		return -EIO;

	if (buffer_id >= this->n_buffers)
		return -EINVAL;

	recycle_buffer(this, buffer_id);

	return 0;
}

static int
impl_node_port_send_command(struct spa_node *node,
			    enum spa_direction direction, uint32_t port_id, const struct spa_command *command)
{
	spa_return_val_if_fail(node != NULL, -EINVAL);
	return -ENOTSUP;
}

static int impl_node_process_input(struct spa_node *node)
{
	return -ENOTSUP;
}

static int impl_node_process_output(struct spa_node *node)
{
	struct state *this;
	struct spa_io_buffers *io;

	spa_return_val_if_fail(node != NULL, -EINVAL);

	this = SPA_CONTAINER_OF(node, struct state, node);
	io = this->io;
	spa_return_val_if_fail(io != NULL, -EIO);

	if (io->status == SPA_STATUS_HAVE_BUFFER)
		return SPA_STATUS_HAVE_BUFFER;

	if (io->buffer_id < this->n_buffers) {
		recycle_buffer(this, io->buffer_id);
		io->buffer_id = SPA_ID_INVALID;
	}
	return 0;
}

static const struct spa_dict_item node_info_items[] = {
	{ "media.class", "Audio/Source" },
};

static const struct spa_dict node_info = {
	node_info_items,
	SPA_N_ELEMENTS(node_info_items)
};

static const struct spa_node impl_node = {
	SPA_VERSION_NODE,
	&node_info,
	impl_node_enum_params,
	impl_node_set_param,
	impl_node_send_command,
	impl_node_set_callbacks,
	impl_node_get_n_ports,
	impl_node_get_port_ids,
	impl_node_add_port,
	impl_node_remove_port,
	impl_node_port_get_info,
	impl_node_port_enum_params,
	impl_node_port_set_param,
	impl_node_port_use_buffers,
	impl_node_port_alloc_buffers,
	impl_node_port_set_io,
	impl_node_port_reuse_buffer,
	impl_node_port_send_command,
	impl_node_process_input,
	impl_node_process_output,
};

static int impl_clock_enum_params(struct spa_clock *clock, uint32_t id, uint32_t *index,
				  struct spa_pod **param,
				  struct spa_pod_builder *builder)
{
	return -ENOTSUP;
}

static int impl_clock_set_param(struct spa_clock *clock,
				uint32_t id, uint32_t flags,
				const struct spa_pod *param)
{
	return -ENOTSUP;
}

static int impl_clock_get_time(struct spa_clock *clock,
			       int32_t *rate,
			       int64_t *ticks,
			       int64_t *monotonic_time)
{
	struct state *this;

	spa_return_val_if_fail(clock != NULL, -EINVAL);

	this = SPA_CONTAINER_OF(clock, struct state, clock);

	if (rate)
		*rate = SPA_USEC_PER_SEC;
	if (ticks)
		*ticks = this->last_ticks;
	if (monotonic_time)
		*monotonic_time = this->last_monotonic;

	return 0;
}

static const struct spa_clock impl_clock = {
	SPA_VERSION_CLOCK,
	NULL,
	SPA_CLOCK_STATE_STOPPED,
	impl_clock_enum_params,
	impl_clock_set_param,
	impl_clock_get_time,
};

static int impl_get_interface(struct spa_handle *handle, uint32_t interface_id, void **interface)
{
	struct state *this;

	spa_return_val_if_fail(handle != NULL, -EINVAL);
	spa_return_val_if_fail(interface != NULL, -EINVAL);

	this = (struct state *) handle;

	if (interface_id == this->type.node)
		*interface = &this->node;
	else if (interface_id == this->type.clock)
		*interface = &this->clock;
	else
		return -ENOENT;

	return 0;
}

static int impl_clear(struct spa_handle *handle)
{
	return 0;
}

static int
impl_init(const struct spa_handle_factory *factory,
	  struct spa_handle *handle,
	  const struct spa_dict *info,
	  const struct spa_support *support,
	  uint32_t n_support)
{
	struct state *this;
	uint32_t i;

	spa_return_val_if_fail(factory != NULL, -EINVAL);
	spa_return_val_if_fail(handle != NULL, -EINVAL);

	handle->get_interface = impl_get_interface;
	handle->clear = impl_clear;

	this = (struct state *) handle;

	for (i = 0; i < n_support; i++) {
		if (strcmp(support[i].type, SPA_TYPE__TypeMap) == 0)
			this->map = support[i].data;
		else if (strcmp(support[i].type, SPA_TYPE__Log) == 0)
			this->log = support[i].data;
		else if (strcmp(support[i].type, SPA_TYPE_LOOP__DataLoop) == 0)
			this->data_loop = support[i].data;
		else if (strcmp(support[i].type, SPA_TYPE_LOOP__MainLoop) == 0)
			this->main_loop = support[i].data;
	}
	if (this->map == NULL) {
		spa_log_error(this->log, "an id-map is needed");
		return -EINVAL;
	}
	if (this->data_loop == NULL) {
		spa_log_error(this->log, "a data loop is needed");
		return -EINVAL;
	}
	if (this->main_loop == NULL) {
		spa_log_error(this->log, "a main loop is needed");
		return -EINVAL;
	}
	init_type(&this->type, this->map);

	this->node = impl_node;
	this->clock = impl_clock;
	this->stream = SND_PCM_STREAM_CAPTURE;
	reset_props(&this->props);

	this->info.flags = SPA_PORT_INFO_FLAG_CAN_USE_BUFFERS |
			   SPA_PORT_INFO_FLAG_LIVE |
			   SPA_PORT_INFO_FLAG_PHYSICAL |
			   SPA_PORT_INFO_FLAG_TERMINAL;

	spa_list_init(&this->free);
	spa_list_init(&this->ready);

	for (i = 0; info && i < info->n_items; i++) {
		if (!strcmp(info->items[i].key, "alsa.card")) {
			snprintf(this->props.device, 63, "%s", info->items[i].value);
		}
	}
	return 0;
}

static const struct spa_interface_info impl_interfaces[] = {
	{SPA_TYPE__Node,},
	{SPA_TYPE__Clock,},
};

static int
impl_enum_interface_info(const struct spa_handle_factory *factory,
			 const struct spa_interface_info **info,
			 uint32_t *index)
{
	spa_return_val_if_fail(factory != NULL, -EINVAL);
	spa_return_val_if_fail(info != NULL, -EINVAL);
	spa_return_val_if_fail(index != NULL, -EINVAL);

	if (*index >= SPA_N_ELEMENTS(impl_interfaces))
		return 0;

	*info = &impl_interfaces[(*index)++];

	return 1;
}

static const struct spa_dict_item info_items[] = {
	{ "factory.author", "Wim Taymans <wim.taymans@gmail.com>" },
	{ "factory.description", "Record audio with the alsa API" },
};

static const struct spa_dict info = {
	info_items,
	SPA_N_ELEMENTS(info_items)
};

const struct spa_handle_factory spa_alsa_source_factory = {
	SPA_VERSION_HANDLE_FACTORY,
	NAME,
	&info,
	sizeof(struct state),
	impl_init,
	impl_enum_interface_info,
};
