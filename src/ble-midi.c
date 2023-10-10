/*
 * BlueALSA - ble-midi.c
 * Copyright (c) 2016-2023 Arkadiusz Bokowy
 *
 * This file is a part of bluez-alsa.
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#include "ble-midi.h"

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "shared/log.h"

/**
 * Determine length of the MIDI message based on the status byte. */
static size_t ble_midi_message_len(uint8_t status) {
	switch (status & 0xF0) {
	case 0x80 /* note off */ :
	case 0x90 /* note on */ :
	case 0xA0 /* polyphonic key pressure */ :
	case 0xB0 /* control change */ :
		return 3;
	case 0xC0 /* program change */ :
	case 0xD0 /* channel pressure */ :
		return 2;
	case 0xE0 /* pitch bend */ :
		return 3;
	case 0xF0 /* system messages */ :
		switch (status) {
		case 0xF0 /* system exclusive start */ :
			/* System exclusive message size is unknown. It is just a
			 * stream of bytes terminated by the system exclusive end
			 * status byte. */
			return SIZE_MAX;
		case 0xF1 /* MIDI timing code */ :
			return 2;
		case 0xF2 /* song position pointer */ :
			return 3;
		case 0xF3 /* song select */ :
			return 2;
		case 0xF6 /* tune request */ :
		case 0xF7 /* system exclusive end */ :
		case 0xF8 /* timing clock */ :
		case 0xFA /* start sequence */ :
		case 0xFB /* continue sequence */ :
		case 0xFC /* stop sequence */ :
		case 0xFE /* active sensing */ :
		case 0xFF /* system reset */ :
			return 1;
		}
	}
	/* invalid status byte */
	return 0;
}

/**
 * Parse BLE-MIDI packet.
 *
 * Before parsing next BLE-MIDI packet, this function should be called until
 * it returns 0 or -1. Alternatively, caller can set the parser structure to
 * all-zeroes, which will reset the parser state.
 *
 * @param bm BLE-MIDI parser structure.
 * @param data BLE-MIDI packet data.
 * @param len Length of the packet data.
 * @return On success, in case when at least one full MIDI message was parsed,
 *   this function returns 1. If the BLE-MIDI packet does not contain any more
 *   (completed) MIDI message, 0 is returned. On error, -1 is returned. */
int ble_midi_parse(struct ble_midi *bm, const uint8_t *data, size_t len) {

	uint8_t *bm_buffer = bm->buffer_midi;
	size_t bm_buffer_size = sizeof(bm->buffer_midi);
	size_t bm_buffer_len = 0;

	uint8_t bm_status = bm->status;
	size_t bm_current_len = bm->current_len;

	/* Check if we've got any data to parse. */
	if (bm_current_len == len)
		goto reset;

	/* If the system exclusive message was not ended in the previous
	 * packet we need to reconstruct fragmented message. */
	if (bm->status_sys) {
		bm_buffer = bm->buffer_sys;
		bm_buffer_size = sizeof(bm->buffer_sys);
		bm_buffer_len = bm->buffer_sys_len;
		bm_status = 0xF0;
	}

	/* Every BLE-MIDI packet shall contain a header byte. */
	if (bm_current_len == 0) {
		/* There should be at least 3 bytes in the packet: header, timestamp
		 * and at least one MIDI message byte. */
		if (len < 3 || (data[0] >> 6) != 0x02) {
			errno = EINVAL;
			goto fail;
		}
		/* Extract most significant 7 bits of the timestamp from the header. */
		bm->ts_high = (data[0] & 0x3F) << 7;
		bm_current_len++;
	}

retry:
	/* Check if we've got BLE-MIDI timestamp byte.
	 * It shall have bit 7 set to 1. */
	if (data[bm_current_len] & 0x80) {

		bm->ts = bm->ts_high | (data[bm_current_len] & 0x7F);
		if (++bm_current_len == len) {
			/* If the timestamp byte is the last byte in the packet,
			 * definitely something is wrong. */
			errno = EINVAL;
			goto fail;
		}

		/* After the timestamp byte, there might be a full MIDI message
		 * with a status byte. It shall have bit 7 set to 1. Otherwise,
		 * it might be a running status MIDI message. */
		if (data[bm_current_len] & 0x80) {

			switch (bm_status = data[bm_current_len]) {
			case 0xF0 /* system exclusive start */ :
				/* System exclusive message needs to be stored in a dedicated buffer.
				 * First of all, it can span multiple BLE-MIDI packets. Secondly, it
				 * can be interleaved with MIDI real-time messages. */
				bm_buffer = bm->buffer_sys;
				bm_buffer_size = sizeof(bm->buffer_sys);
				bm_buffer_len = bm->buffer_sys_len;
				bm->ts_sys = bm->ts;
				bm->status_sys = true;
				break;
			case 0xF7 /* system exclusive end */ :
				bm->status_sys = false;
				break;
			}

			/* Store full MIDI message status byte in the buffer. */
			if (bm_buffer_len < bm_buffer_size)
				bm_buffer[bm_buffer_len++] = bm_status;

			if (++bm_current_len == len)
				goto final;

		}

	}

	/* Fix for BLE-MIDI vs MIDI incompatible running status. */
	if (bm_buffer_len == 0 && bm->status_restore) {
		bm_buffer[bm_buffer_len++] = bm_status;
		bm->status_restore = false;
	}

	size_t midi_msg_len;
	if ((midi_msg_len = ble_midi_message_len(bm_status)) == 0) {
		errno = ENOMSG;
		goto fail;
	}

	/* Extract MIDI message data bytes. All these bytes shall have
	 * bit 7 set to 0. */
	while (--midi_msg_len > 0 && !(data[bm_current_len] & 0x80) &&
			/* Make sure that we do not overflow the buffer. */
			bm_buffer_len < bm_buffer_size) {
		bm_buffer[bm_buffer_len++] = data[bm_current_len];
		if (++bm_current_len == len)
			goto final;
	}

	/* MIDI message cannot be incomplete. */
	if (midi_msg_len != 0 && bm_status != 0xF0) {
		errno = EBADMSG;
		goto fail;
	}

	if (bm_buffer_len == bm_buffer_size) {
		warn("BLE-MIDI message too long: %zu", bm_buffer_size);
		errno = EMSGSIZE;
		goto final;
	}

	/* This parser reads only one MIDI message at a time. However, in case of
	 * the system exclusive message, instead of returning not-completed MIDI
	 * message, check if the message was ended in this BLE-MIDI packet. */
	if (bm_status == 0xF0) {
		bm->buffer_sys_len = bm_buffer_len;
		goto retry;
	}

final:

	bm->buffer = bm_buffer;
	bm->len = bm_buffer_len;

	/* In BLE-MIDI, MIDI real-time messages and MIDI common messages do not
	 * affect the running status. For simplicity, we will not store running
	 * status for every system message. */
	if ((bm_status & 0xF0) != 0xF0)
		bm->status = bm_status;

	/* According to the BLE-MIDI specification, the running status is not
	 * cancelled by the system common messages. However, for MIDI, running
	 * status is not cancelled by the system real-time messages only. So,
	 * for everything other than the system real-time messages, we need to
	 * insert the status byte into the buffer. */
	if (bm_status >= 0xF0 && bm_status < 0xF8)
		bm->status_restore = true;

	bm->current_len = bm_current_len;

	switch (bm_status) {
	case 0xF0 /* system exclusive start */ :
		bm->buffer_sys_len = bm_buffer_len;
		goto reset;
	case 0xF7 /* system exclusive end */ :
		bm->buffer_sys_len = 0;
		bm->ts = bm->ts_sys;
		break;
	}

	return 1;

reset:
	bm->current_len = 0;
	return 0;

fail:
	bm->current_len = 0;
	return -1;
}
