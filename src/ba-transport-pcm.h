/*
 * BlueALSA - ba-transport-pcm.h
 * Copyright (c) 2016-2023 Arkadiusz Bokowy
 *
 * This file is a part of bluez-alsa.
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#pragma once
#ifndef BLUEALSA_BATRANSPORTPCM_H_
#define BLUEALSA_BATRANSPORTPCM_H_

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include <glib.h>

enum ba_transport_pcm_mode {
	/* PCM used for capturing audio */
	BA_TRANSPORT_PCM_MODE_SOURCE,
	/* PCM used for playing audio */
	BA_TRANSPORT_PCM_MODE_SINK,
};

/**
 * Builder for 16-bit PCM stream format identifier. */
#define BA_TRANSPORT_PCM_FORMAT(sign, width, bytes, endian) \
	(((sign & 1) << 15) | ((endian & 1) << 14) | ((bytes & 0x3F) << 8) | (width & 0xFF))

#define BA_TRANSPORT_PCM_FORMAT_SIGN(format)   (((format) >> 15) & 0x1)
#define BA_TRANSPORT_PCM_FORMAT_WIDTH(format)  ((format) & 0xFF)
#define BA_TRANSPORT_PCM_FORMAT_BYTES(format)  (((format) >> 8) & 0x3F)
#define BA_TRANSPORT_PCM_FORMAT_ENDIAN(format) (((format) >> 14) & 0x1)

#define BA_TRANSPORT_PCM_FORMAT_U8      BA_TRANSPORT_PCM_FORMAT(0, 8, 1, 0)
#define BA_TRANSPORT_PCM_FORMAT_S16_2LE BA_TRANSPORT_PCM_FORMAT(1, 16, 2, 0)
#define BA_TRANSPORT_PCM_FORMAT_S24_3LE BA_TRANSPORT_PCM_FORMAT(1, 24, 3, 0)
#define BA_TRANSPORT_PCM_FORMAT_S24_4LE BA_TRANSPORT_PCM_FORMAT(1, 24, 4, 0)
#define BA_TRANSPORT_PCM_FORMAT_S32_4LE BA_TRANSPORT_PCM_FORMAT(1, 32, 4, 0)

struct ba_transport_pcm_volume {
	/* volume level change in "dB * 100" */
	int level;
	/* audio signal mute switches */
	bool soft_mute;
	bool hard_mute;
	/* calculated PCM scale factor based on decibel formula
	 * pow(10, dB / 20); for muted channel it shall equal 0 */
	double scale;
};

struct ba_transport_thread;

struct ba_transport_pcm {

	/* backward reference to transport */
	struct ba_transport *t;
	/* associated transport thread */
	struct ba_transport_thread *th;

	/* PCM stream operation mode */
	enum ba_transport_pcm_mode mode;

	/* guard PCM data updates */
	pthread_mutex_t mutex;
	/* updates notification */
	pthread_cond_t cond;

	/* FIFO file descriptor */
	int fd;

	/* indicates whether PCM shall be active */
	bool active;

	/* 16-bit stream format identifier */
	uint16_t format;
	/* number of audio channels */
	unsigned int channels;
	/* PCM sampling frequency */
	unsigned int sampling;

	/* Overall PCM delay in 1/10 of millisecond, caused by
	 * audio encoding or decoding and data transfer. */
	unsigned int delay;

	/* guard delay adjustments access */
	pthread_mutex_t delay_adjustments_mtx;
	/* PCM delay adjustments in 1/10 of millisecond, set by client API to allow
	 * user correction of delay reporting inaccuracy. */
	GHashTable *delay_adjustments;

	/* indicates whether FIFO buffer was synchronized */
	bool synced;

	/* internal software volume control */
	bool soft_volume;

	/* Volume configuration for channel left [0] and right [1]. In case of
	 * a monophonic sound, only the left [0] channel shall be used. */
	struct ba_transport_pcm_volume volume[2];

	/* new PCM client mutex */
	pthread_mutex_t client_mtx;

	/* exported PCM D-Bus API */
	char *ba_dbus_path;
	bool ba_dbus_exported;

};

int transport_pcm_init(
		struct ba_transport_pcm *pcm,
		enum ba_transport_pcm_mode mode,
		struct ba_transport_thread *th);
void transport_pcm_free(
		struct ba_transport_pcm *pcm);

/**
 * Transport PCM encoder/decoder IO thread function. */
typedef void *(*ba_transport_pcm_thread_func)(struct ba_transport_pcm *);

#define debug_transport_pcm_thread_loop(pcm, tag) \
	debug("PCM IO loop: %s: %s: %s", tag, __func__, ba_transport_debug_name((pcm)->t))

void ba_transport_pcm_thread_cleanup(struct ba_transport_pcm *pcm);

struct ba_transport_pcm *ba_transport_pcm_ref(struct ba_transport_pcm *pcm);
void ba_transport_pcm_unref(struct ba_transport_pcm *pcm);

int ba_transport_pcm_start(
		struct ba_transport_pcm *pcm,
		ba_transport_pcm_thread_func th_func,
		const char *name,
		bool master);

int ba_transport_pcm_release(struct ba_transport_pcm *pcm);

int ba_transport_pcm_pause(struct ba_transport_pcm *pcm);
int ba_transport_pcm_resume(struct ba_transport_pcm *pcm);
int ba_transport_pcm_drain(struct ba_transport_pcm *pcm);
int ba_transport_pcm_drop(struct ba_transport_pcm *pcm);

bool ba_transport_pcm_is_active(const struct ba_transport_pcm *pcm);

int ba_transport_pcm_volume_level_to_range(int value, int max);
int ba_transport_pcm_volume_range_to_level(int value, int max);

void ba_transport_pcm_volume_set(
		struct ba_transport_pcm_volume *volume,
		const int *level,
		const bool *soft_mute,
		const bool *hard_mute);

int ba_transport_pcm_volume_update(
		struct ba_transport_pcm *pcm);

int ba_transport_pcm_get_delay(
		const struct ba_transport_pcm *pcm);

int16_t ba_transport_pcm_delay_adjustment_get(
		const struct ba_transport_pcm *pcm);
void ba_transport_pcm_delay_adjustment_set(
		struct ba_transport_pcm *pcm,
		uint16_t codec_id,
		int16_t adjustment);

#endif
