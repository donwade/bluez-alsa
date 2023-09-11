/*
 * BlueALSA - ba-transport-midi.h
 * Copyright (c) 2016-2023 Arkadiusz Bokowy
 *
 * This file is a part of bluez-alsa.
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#pragma once
#ifndef BLUEALSA_BATRANSPORTMIDI_H_
#define BLUEALSA_BATRANSPORTMIDI_H_

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <pthread.h>
#include <stdbool.h>

enum ba_transport_midi_mode {
	BA_TRANSPORT_MIDI_MODE_INPUT,
	BA_TRANSPORT_MIDI_MODE_OUTPUT,
};

struct ba_transport;

struct ba_transport_midi {

	/* backward reference to transport */
	struct ba_transport *t;

	/* MIDI operation mode */
	enum ba_transport_midi_mode mode;

	/* guard MIDI data updates */
	pthread_mutex_t mutex;

	/* FIFO file descriptor */
	int fd;
	/* FIFO file descriptor watch ID */
	unsigned int fd_watch_id;

	/* new MIDI client mutex */
	pthread_mutex_t client_mtx;

	/* exported MIDI D-Bus API */
	char *ba_dbus_path;
	bool ba_dbus_exported;

};

int transport_midi_init(
		struct ba_transport_midi *midi,
		enum ba_transport_midi_mode mode,
		struct ba_transport *t);
void transport_midi_free(
		struct ba_transport_midi *midi);

struct ba_transport_midi *ba_transport_midi_ref(struct ba_transport_midi *midi);
void ba_transport_midi_unref(struct ba_transport_midi *midi);

#endif
