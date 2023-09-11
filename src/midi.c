/*
 * BlueALSA - midi.c
 * Copyright (c) 2016-2023 Arkadiusz Bokowy
 *
 * This file is a part of bluez-alsa.
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#include "midi.h"

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

#include "ba-transport.h"
#include "ba-transport-midi.h"
#include "ble-midi.h"
#include "shared/log.h"

static gboolean midi_watch_ble_midi(GIOChannel *ch,
		G_GNUC_UNUSED GIOCondition condition, void *userdata) {

	struct ba_transport *t = userdata;
	GError *err = NULL;
	uint8_t data[512];
	size_t len;

	switch (g_io_channel_read_chars(ch, (char *)data, sizeof(data), &len, &err)) {
	case G_IO_STATUS_AGAIN:
		return TRUE;
	case G_IO_STATUS_ERROR:
		error("BLE-MIDI link read error: %s", err->message);
		g_error_free(err);
		return TRUE;
	case G_IO_STATUS_NORMAL:
		break;
	case G_IO_STATUS_EOF:
		/* remove channel from watch */
		return FALSE;
	}

	for (;;) {

		int rv;
		if ((rv = ble_midi_parse(&t->midi.parser, data, len)) <= 0) {
			if (rv == -1)
				error("Couldn't parse BLE-MIDI packet: %s", strerror(errno));
			break;
		}

		pthread_mutex_lock(&t->midi.midi_in.mutex);

		if (t->midi.midi_in.fd != -1)
			write(t->midi.midi_in.fd, t->midi.parser.buffer, t->midi.parser.len);

		pthread_mutex_unlock(&t->midi.midi_in.mutex);

	}

	return TRUE;
}

static gboolean midi_watch_input(G_GNUC_UNUSED GIOChannel *ch,
		G_GNUC_UNUSED GIOCondition condition, void *userdata) {

	struct ba_transport *t = userdata;

	pthread_mutex_lock(&t->midi.midi_in.mutex);
	debug("Closing MIDI input: %d", t->midi.midi_in.fd);
	t->midi.midi_in.fd = -1;
	t->midi.midi_in.fd_watch_id = 0;
	pthread_mutex_unlock(&t->midi.midi_in.mutex);

	/* remove channel from watch */
	return FALSE;
}

static gboolean midi_watch_output(GIOChannel *ch,
		G_GNUC_UNUSED GIOCondition condition, void *userdata) {

	struct ba_transport *t = userdata;
	GError *err = NULL;
	uint8_t data[512];
	size_t len;

	switch (g_io_channel_read_chars(ch, (char *)data, sizeof(data), &len, &err)) {
	case G_IO_STATUS_AGAIN:
		return TRUE;
	case G_IO_STATUS_ERROR:
		error("MIDI output read error: %s", err->message);
		g_error_free(err);
		return TRUE;
	case G_IO_STATUS_NORMAL:
		break;
	case G_IO_STATUS_EOF:
		pthread_mutex_lock(&t->midi.midi_out.mutex);
		debug("Closing MIDI output: %d", t->midi.midi_out.fd);
		t->midi.midi_out.fd = -1;
		t->midi.midi_out.fd_watch_id = 0;
		pthread_mutex_unlock(&t->midi.midi_out.mutex);
		/* remove channel from watch */
		return FALSE;
	}

	/* Write data to the BLE-MIDI link only if the link is established,
	 * which is indicated by the non-zero MTU value. */
	if (t->bt_fd != -1 && t->mtu_write != 0)
		write(t->bt_fd, data, len);

	return TRUE;
}

int midi_transport_start(struct ba_transport *t) {

	/* Reset BLE-MIDI parser state. */
	memset(&t->midi.parser, 0, sizeof(t->midi.parser));

	pthread_mutex_lock(&t->midi.midi_in.mutex);
	pthread_mutex_lock(&t->midi.midi_out.mutex);

	if (t->midi.fd_watch_id == 0 &&
			t->bt_fd != -1) {

		GIOChannel *ch = g_io_channel_unix_new(t->bt_fd);
		g_io_channel_set_close_on_unref(ch, TRUE);
		g_io_channel_set_encoding(ch, NULL, NULL);

		debug("Starting BLE-MIDI IO watch: %d", t->bt_fd);
		t->midi.fd_watch_id = g_io_add_watch_full(ch, G_PRIORITY_HIGH,
				G_IO_IN, midi_watch_ble_midi, ba_transport_ref(t),
				(GDestroyNotify)ba_transport_unref);
		g_io_channel_unref(ch);

	}

	/* When the reading end of a FIFO is closed, poll reports error condition.
	 * This IO watch will allow us to cleanup stuff on our side. */
	if (t->midi.midi_in.fd_watch_id == 0 &&
			t->midi.midi_in.fd != -1) {

		GIOChannel *ch = g_io_channel_unix_new(t->midi.midi_in.fd);
		g_io_channel_set_close_on_unref(ch, TRUE);

		debug("Starting MIDI input IO watch: %d", t->midi.midi_in.fd);
		t->midi.midi_in.fd_watch_id = g_io_add_watch_full(ch, G_PRIORITY_HIGH,
				G_IO_ERR | G_IO_HUP, midi_watch_input, ba_transport_ref(t),
				(GDestroyNotify)ba_transport_unref);
		g_io_channel_unref(ch);

	}

	if (t->midi.midi_out.fd_watch_id == 0 &&
			t->midi.midi_out.fd != -1) {

		GIOChannel *ch = g_io_channel_unix_new(t->midi.midi_out.fd);
		g_io_channel_set_close_on_unref(ch, TRUE);
		g_io_channel_set_encoding(ch, NULL, NULL);

		debug("Starting MIDI output IO watch: %d", t->midi.midi_out.fd);
		t->midi.midi_out.fd_watch_id = g_io_add_watch_full(ch, G_PRIORITY_HIGH,
				G_IO_IN | G_IO_ERR | G_IO_HUP, midi_watch_output, ba_transport_ref(t),
				(GDestroyNotify)ba_transport_unref);
		g_io_channel_unref(ch);

	}

	pthread_mutex_unlock(&t->midi.midi_in.mutex);
	pthread_mutex_unlock(&t->midi.midi_out.mutex);

	return 0;
}

int midi_transport_stop(struct ba_transport *t) {

	if (t->midi.fd_watch_id != 0)
		g_source_remove(t->midi.fd_watch_id);
	if (t->midi.midi_in.fd_watch_id != 0)
		g_source_remove(t->midi.midi_in.fd_watch_id);
	if (t->midi.midi_out.fd_watch_id != 0)
		g_source_remove(t->midi.midi_out.fd_watch_id);

	return 0;
}
