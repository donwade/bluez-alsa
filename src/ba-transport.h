/*
 * BlueALSA - ba-transport.h
 * Copyright (c) 2016-2023 Arkadiusz Bokowy
 *
 * This file is a part of bluez-alsa.
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#pragma once
#ifndef BLUEALSA_BATRANSPORT_H_
#define BLUEALSA_BATRANSPORT_H_

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "a2dp.h"
#include "ba-device.h"
#include "ba-transport-pcm.h"
#include "bluez.h"
#include "shared/a2dp-codecs.h"

enum ba_transport_thread_state {
	BA_TRANSPORT_THREAD_STATE_IDLE,
	BA_TRANSPORT_THREAD_STATE_STARTING,
	BA_TRANSPORT_THREAD_STATE_RUNNING,
	BA_TRANSPORT_THREAD_STATE_STOPPING,
	BA_TRANSPORT_THREAD_STATE_JOINING,
	BA_TRANSPORT_THREAD_STATE_TERMINATED,
};

enum ba_transport_thread_signal {
	BA_TRANSPORT_THREAD_SIGNAL_PING,
	BA_TRANSPORT_THREAD_SIGNAL_PCM_OPEN,
	BA_TRANSPORT_THREAD_SIGNAL_PCM_CLOSE,
	BA_TRANSPORT_THREAD_SIGNAL_PCM_PAUSE,
	BA_TRANSPORT_THREAD_SIGNAL_PCM_RESUME,
	BA_TRANSPORT_THREAD_SIGNAL_PCM_SYNC,
	BA_TRANSPORT_THREAD_SIGNAL_PCM_DROP,
};

struct ba_transport_thread {

	/* backward reference to transport */
	struct ba_transport *t;
	/* associated PCM */
	struct ba_transport_pcm *pcm;

	/* guard transport thread data updates */
	pthread_mutex_t mutex;
	/* state/id updates notification */
	pthread_cond_t cond;

	/* current state of the thread */
	enum ba_transport_thread_state state;

	/* actual thread ID */
	pthread_t id;
	/* indicates a master thread */
	bool master;
	/* clone of BT socket */
	int bt_fd;
	/* notification PIPE */
	int pipe[2];

};

int ba_transport_thread_state_set(
		struct ba_transport_thread *th,
		enum ba_transport_thread_state state);

#define ba_transport_thread_state_set_idle(th) \
	ba_transport_thread_state_set(th, BA_TRANSPORT_THREAD_STATE_IDLE)
#define ba_transport_thread_state_set_running(th) \
	ba_transport_thread_state_set(th, BA_TRANSPORT_THREAD_STATE_RUNNING)
#define ba_transport_thread_state_set_stopping(th) \
	ba_transport_thread_state_set(th, BA_TRANSPORT_THREAD_STATE_STOPPING)

bool ba_transport_thread_state_check(
		const struct ba_transport_thread *th,
		enum ba_transport_thread_state state);

#define ba_transport_thread_state_check_idle(th) \
	ba_transport_thread_state_check(th, BA_TRANSPORT_THREAD_STATE_IDLE)
#define ba_transport_thread_state_check_running(th) \
	ba_transport_thread_state_check(th, BA_TRANSPORT_THREAD_STATE_RUNNING)
#define ba_transport_thread_state_check_terminated(th) \
	ba_transport_thread_state_check(th, BA_TRANSPORT_THREAD_STATE_TERMINATED)

int ba_transport_thread_state_wait(
		const struct ba_transport_thread *th,
		enum ba_transport_thread_state state);

#define ba_transport_thread_state_wait_running(th) \
	ba_transport_thread_state_wait(th, BA_TRANSPORT_THREAD_STATE_RUNNING)
#define ba_transport_thread_state_wait_terminated(th) \
	ba_transport_thread_state_wait(th, BA_TRANSPORT_THREAD_STATE_TERMINATED)

int ba_transport_thread_bt_acquire(
		struct ba_transport_thread *th);
int ba_transport_thread_bt_release(
		struct ba_transport_thread *th);

int ba_transport_thread_signal_send(
		struct ba_transport_thread *th,
		enum ba_transport_thread_signal signal);
int ba_transport_thread_signal_recv(
		struct ba_transport_thread *th,
		enum ba_transport_thread_signal *signal);

enum ba_transport_thread_manager_command {
	BA_TRANSPORT_THREAD_MANAGER_TERMINATE = 0,
	BA_TRANSPORT_THREAD_MANAGER_CANCEL_THREADS,
	BA_TRANSPORT_THREAD_MANAGER_CANCEL_IF_NO_CLIENTS,
};

enum ba_transport_profile {
	BA_TRANSPORT_PROFILE_NONE        = 0,
	BA_TRANSPORT_PROFILE_A2DP_SOURCE = (1 << 0),
	BA_TRANSPORT_PROFILE_A2DP_SINK   = (2 << 0),
	BA_TRANSPORT_PROFILE_HFP_HF      = (1 << 2),
	BA_TRANSPORT_PROFILE_HFP_AG      = (2 << 2),
	BA_TRANSPORT_PROFILE_HSP_HS      = (1 << 4),
	BA_TRANSPORT_PROFILE_HSP_AG      = (2 << 4),
};

#define BA_TRANSPORT_PROFILE_MASK_A2DP \
	(BA_TRANSPORT_PROFILE_A2DP_SOURCE | BA_TRANSPORT_PROFILE_A2DP_SINK)
#define BA_TRANSPORT_PROFILE_MASK_HFP \
	(BA_TRANSPORT_PROFILE_HFP_HF | BA_TRANSPORT_PROFILE_HFP_AG)
#define BA_TRANSPORT_PROFILE_MASK_HSP \
	(BA_TRANSPORT_PROFILE_HSP_HS | BA_TRANSPORT_PROFILE_HSP_AG)
#define BA_TRANSPORT_PROFILE_MASK_SCO \
	(BA_TRANSPORT_PROFILE_MASK_HFP | BA_TRANSPORT_PROFILE_MASK_HSP)
#define BA_TRANSPORT_PROFILE_MASK_AG \
	(BA_TRANSPORT_PROFILE_HSP_AG | BA_TRANSPORT_PROFILE_HFP_AG)
#define BA_TRANSPORT_PROFILE_MASK_HF \
	(BA_TRANSPORT_PROFILE_HSP_HS | BA_TRANSPORT_PROFILE_HFP_HF)

struct ba_transport {

	/* backward reference to device */
	struct ba_device *d;

	/* Transport structure covers all transports supported by BlueALSA. However,
	 * every transport requires specific handling - link acquisition, transport
	 * specific configuration, freeing resources, etc. */
	enum ba_transport_profile profile;

	/* guard modifications of transport codec */
	pthread_mutex_t codec_id_mtx;
	/* For A2DP vendor codecs the upper byte of the codec field
	 * contains the lowest byte of the vendor ID. */
	uint16_t codec_id;

	/* synchronization for codec selection */
	pthread_mutex_t codec_select_client_mtx;

	/* data for D-Bus management */
	char *bluez_dbus_owner;
	char *bluez_dbus_path;

	/* guard modifications of our file descriptor
	 * and the IO threads stopping flag */
	pthread_mutex_t bt_fd_mtx;

	/* Ensure BT file descriptor acquisition procedure
	 * is completed atomically. */
	pthread_mutex_t acquisition_mtx;

	/* This field stores a file descriptor (socket) associated with the BlueZ
	 * side of the transport. The role of this socket depends on the transport
	 * type - it can be either A2DP or SCO link. */
	int bt_fd;

	/* max transfer unit values for bt_fd */
	size_t mtu_read;
	size_t mtu_write;

	/* threads for audio processing */
	struct ba_transport_thread thread_enc;
	struct ba_transport_thread thread_dec;

	/* thread for managing IO threads */
	pthread_t thread_manager_thread_id;
	int thread_manager_pipe[2];

	/* indicates IO threads stopping */
	pthread_cond_t stopped;
	bool stopping;

	union {

		struct {

			/* used D-Bus endpoint path */
			const char *bluez_dbus_sep_path;

			/* current state of the transport */
			enum bluez_a2dp_transport_state state;

			/* audio codec configuration capabilities */
			const struct a2dp_codec *codec;
			/* selected audio codec configuration */
			a2dp_t configuration;

			/* delay reported by BlueZ */
			uint16_t delay;
			/* volume reported by BlueZ */
			uint16_t volume;

			struct ba_transport_pcm pcm;
			/* PCM for back-channel stream */
			struct ba_transport_pcm pcm_bc;

			/* Value reported by the ioctl(TIOCOUTQ) when the output buffer is
			 * empty. Somehow this ioctl call reports "available" buffer space.
			 * So, in order to get the number of bytes in the queue buffer, we
			 * have to subtract the initial value from values returned by
			 * subsequent ioctl() calls. */
			int bt_fd_coutq_init;

		} a2dp;

		struct {

			/* Associated RFCOMM thread for SCO transport handled by local
			 * HSP/HFP implementation. Otherwise, this field is set to NULL. */
			struct ba_rfcomm *rfcomm;

#if ENABLE_OFONO
			/* Associated oFono card and modem paths. In case when SCO transport
			 * is not oFono-based, these fields are set to NULL. */
			char *ofono_dbus_path_card;
			char *ofono_dbus_path_modem;
#endif

			/* Speaker and microphone signals should to be exposed as
			 * a separate PCM devices. Hence, there is a requirement
			 * for separate configurations.
			 *
			 * NOTE: The speaker/microphone notation always refers to the whole
			 *       AG/HS setup. For AG the speaker is an outgoing audio stream,
			 *       while for HS the speaker is an incoming audio stream. */
			struct ba_transport_pcm pcm_spk;
			struct ba_transport_pcm pcm_mic;

			/* time-stamp when the SCO link has been closed */
			struct timespec closed_at;

		} sco;

	};

	/* callback functions for self-management */
	int (*acquire)(struct ba_transport *);
	int (*release)(struct ba_transport *);

	/* memory self-management */
	int ref_count;

};

struct ba_transport *ba_transport_new_a2dp(
		struct ba_device *device,
		enum ba_transport_profile profile,
		const char *dbus_owner,
		const char *dbus_path,
		const struct a2dp_codec *codec,
		const void *configuration);
struct ba_transport *ba_transport_new_sco(
		struct ba_device *device,
		enum ba_transport_profile profile,
		const char *dbus_owner,
		const char *dbus_path,
		int rfcomm_fd);

#if DEBUG
const char *ba_transport_debug_name(
		const struct ba_transport *t);
#endif

struct ba_transport *ba_transport_lookup(
		const struct ba_device *device,
		const char *dbus_path);
struct ba_transport *ba_transport_ref(
		struct ba_transport *t);

void ba_transport_destroy(struct ba_transport *t);
void ba_transport_unref(struct ba_transport *t);

int ba_transport_select_codec_a2dp(
		struct ba_transport *t,
		const struct a2dp_sep *sep);
int ba_transport_select_codec_sco(
		struct ba_transport *t,
		uint16_t codec_id);

uint16_t ba_transport_get_codec(
		const struct ba_transport *t);
void ba_transport_set_codec(
		struct ba_transport *t,
		uint16_t codec_id);

int ba_transport_start(struct ba_transport *t);
int ba_transport_stop(struct ba_transport *t);
int ba_transport_stop_async(struct ba_transport *t);
int ba_transport_stop_if_no_clients(struct ba_transport *t);

int ba_transport_acquire(struct ba_transport *t);
int ba_transport_release(struct ba_transport *t);

int ba_transport_set_a2dp_state(
		struct ba_transport *t,
		enum bluez_a2dp_transport_state state);

#endif
