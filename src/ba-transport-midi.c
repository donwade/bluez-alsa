/*
 * BlueALSA - ba-transport-midi.c
 * Copyright (c) 2016-2023 Arkadiusz Bokowy
 *
 * This file is a part of bluez-alsa.
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#include "ba-transport-midi.h"

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <pthread.h>
#include <stddef.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>

#include <glib.h>

#include "ba-adapter.h"
#include "ba-device.h"
#include "ba-transport.h"

int transport_midi_init(
		struct ba_transport_midi *midi,
		enum ba_transport_midi_mode mode,
		struct ba_transport *t) {

	midi->t = t;
	midi->mode = mode;
	midi->fd = -1;

	pthread_mutex_init(&midi->mutex, NULL);
	pthread_mutex_init(&midi->client_mtx, NULL);

	/* Check whether transport is attached to the "local" device. If so,
	 * we will use the adapter path instead of device path as a base for
	 * out MIDI D-Bus object path. */
	const bool is_local = bacmp(&t->d->addr, &t->d->a->hci.bdaddr) == 0;
	midi->ba_dbus_path = g_strdup_printf("%s/midi/%s",
			is_local ?  t->d->a->ba_dbus_path : t->d->ba_dbus_path,
			mode == BA_TRANSPORT_MIDI_MODE_INPUT ? "input" : "output");

	return 0;
}

void transport_midi_free(
		struct ba_transport_midi *midi) {

	pthread_mutex_destroy(&midi->mutex);
	pthread_mutex_destroy(&midi->client_mtx);

	g_free(midi->ba_dbus_path);

}

struct ba_transport_midi *ba_transport_midi_ref(struct ba_transport_midi *midi) {
	ba_transport_ref(midi->t);
	return midi;
}

void ba_transport_midi_unref(struct ba_transport_midi *midi) {
	ba_transport_unref(midi->t);
}
