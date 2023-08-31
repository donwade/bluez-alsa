/*
 * test-ble-midi.c
 * Copyright (c) 2016-2023 Arkadiusz Bokowy
 *
 * This file is a part of bluez-alsa.
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <check.h>

#include "ble-midi.h"

#include "inc/check.inc"

CK_START_TEST(test_ble_midi_parse_single) {

	const uint8_t data[] = { 0x85, 0x81, 0xC0, 0x42 };
	const uint8_t midi[] = { 0xC0, 0x42 };

	struct ble_midi bm = { 0 };

	ck_assert_uint_eq(ble_midi_parse(&bm, data, sizeof(data)), 1);
	ck_assert_uint_eq(ble_midi_parse(&bm, data, sizeof(data)), 0);

	ck_assert_uint_eq(bm.ts, 0x0281);
	ck_assert_uint_eq(bm.len, sizeof(midi));
	ck_assert_mem_eq(bm.buffer, midi, sizeof(midi));

} CK_END_TEST

CK_START_TEST(test_ble_midi_parse_multiple) {

	const uint8_t data1[] = { 0x80, 0x81, 0x90, 0x40, 0x7f };
	const uint8_t data2[] = { 0x80, 0x82, 0xA0, 0x40, 0x7f };
	const uint8_t midi1[] = { 0x90, 0x40, 0x7f };
	const uint8_t midi2[] = { 0xA0, 0x40, 0x7f };

	struct ble_midi bm = { 0 };

	ck_assert_uint_eq(ble_midi_parse(&bm, data1, sizeof(data1)), 1);
	ck_assert_uint_eq(ble_midi_parse(&bm, data1, sizeof(data1)), 0);

	ck_assert_uint_eq(bm.ts, 0x0001);
	ck_assert_uint_eq(bm.len, sizeof(midi1));
	ck_assert_mem_eq(bm.buffer, midi1, sizeof(midi1));

	ck_assert_uint_eq(ble_midi_parse(&bm, data2, sizeof(data2)), 1);
	ck_assert_uint_eq(ble_midi_parse(&bm, data2, sizeof(data2)), 0);

	ck_assert_uint_eq(bm.ts, 0x0002);
	ck_assert_uint_eq(bm.len, sizeof(midi2));
	ck_assert_mem_eq(bm.buffer, midi2, sizeof(midi2));

} CK_END_TEST

CK_START_TEST(test_ble_midi_parse_invalid_header) {

	const uint8_t data[] = { 0x10, 0x80, 0x90, 0x40, 0x7f };

	struct ble_midi bm = { 0 };
	ck_assert_uint_eq(ble_midi_parse(&bm, data, sizeof(data)), -1);

} CK_END_TEST

CK_START_TEST(test_ble_midi_parse_invalid_status) {

	const uint8_t data[] = { 0x80, 0x80, 0x40, 0x40, 0x7f };

	struct ble_midi bm = { 0 };
	ck_assert_uint_eq(ble_midi_parse(&bm, data, sizeof(data)), -1);

} CK_END_TEST

CK_START_TEST(test_ble_midi_parse_invalid_interleaved_real_time) {

	const uint8_t data[] = { 0x80, 0x80, 0x90, 0x40, 0xF8, 0x7f };

	struct ble_midi bm = { 0 };
	ck_assert_uint_eq(ble_midi_parse(&bm, data, sizeof(data)), -1);

} CK_END_TEST

CK_START_TEST(test_ble_midi_parse_single_joined) {

	const uint8_t data[] = { 0x80, 0x81, 0x90, 0x40, 0x7f, 0x81, 0xE0, 0x10, 0x42 };
	const uint8_t midi1[] = { 0x90, 0x40, 0x7f };
	const uint8_t midi2[] = { 0xE0, 0x10, 0x42 };

	struct ble_midi bm = { 0 };

	ck_assert_uint_eq(ble_midi_parse(&bm, data, sizeof(data)), 1);
	ck_assert_uint_eq(bm.ts, 0x0001);
	ck_assert_uint_eq(bm.len, sizeof(midi1));
	ck_assert_mem_eq(bm.buffer, midi1, sizeof(midi1));

	ck_assert_uint_eq(ble_midi_parse(&bm, data, sizeof(data)), 1);
	ck_assert_uint_eq(bm.ts, 0x0001);
	ck_assert_uint_eq(bm.len, sizeof(midi2));
	ck_assert_mem_eq(bm.buffer, midi2, sizeof(midi2));

} CK_END_TEST

CK_START_TEST(test_ble_midi_parse_single_real_time) {

	const uint8_t data[] = { 0x80, 0x81, 0xFF };
	const uint8_t midi[] = { 0xFF };

	struct ble_midi bm = { 0 };

	ck_assert_uint_eq(ble_midi_parse(&bm, data, sizeof(data)), 1);
	ck_assert_uint_eq(bm.ts, 0x0001);
	ck_assert_uint_eq(bm.len, sizeof(midi));
	ck_assert_mem_eq(bm.buffer, midi, sizeof(midi));

} CK_END_TEST

CK_START_TEST(test_ble_midi_parse_multiple_real_time) {

	const uint8_t data1[] = { 0x80, 0x81, 0xF3, 0x01 };
	const uint8_t data2[] = { 0x80, 0x81, 0xF2, 0x7F, 0x7F };
	const uint8_t midi1[] = { 0xF3, 0x01 };
	const uint8_t midi2[] = { 0xF2, 0x7F, 0x7F };

	struct ble_midi bm = { 0 };

	ck_assert_uint_eq(ble_midi_parse(&bm, data1, sizeof(data1)), 1);
	ck_assert_uint_eq(ble_midi_parse(&bm, data1, sizeof(data1)), 0);
	ck_assert_uint_eq(bm.ts, 0x0001);
	ck_assert_uint_eq(bm.len, sizeof(midi1));
	ck_assert_mem_eq(bm.buffer, midi1, sizeof(midi1));

	ck_assert_uint_eq(ble_midi_parse(&bm, data2, sizeof(data2)), 1);
	ck_assert_uint_eq(ble_midi_parse(&bm, data2, sizeof(data2)), 0);
	ck_assert_uint_eq(bm.ts, 0x0001);
	ck_assert_uint_eq(bm.len, sizeof(midi2));
	ck_assert_mem_eq(bm.buffer, midi2, sizeof(midi2));

} CK_END_TEST

CK_START_TEST(test_ble_midi_parse_single_system_exclusive) {

	const uint8_t data[] = { 0x80, 0x81, 0xF0, 0x01, 0x02, 0x81, 0xF7 };
	const uint8_t midi[] = { 0xF0, 0x01, 0x02, 0xF7 };

	struct ble_midi bm = { 0 };

	ck_assert_uint_eq(ble_midi_parse(&bm, data, sizeof(data)), 1);
	ck_assert_uint_eq(ble_midi_parse(&bm, data, sizeof(data)), 0);

	ck_assert_uint_eq(bm.ts, 0x0001);
	ck_assert_uint_eq(bm.len, sizeof(midi));
	ck_assert_mem_eq(bm.buffer, midi, sizeof(midi));

} CK_END_TEST

CK_START_TEST(test_ble_midi_parse_multiple_system_exclusive) {

	const uint8_t data1[] = { 0x80, 0x81, 0xF0, 0x01, 0x02, 0x03 };
	const uint8_t data2[] = { 0x80, 0x04, 0x05, 0x82, 0xF7 };
	const uint8_t midi[] = { 0xF0, 0x01, 0x02, 0x03, 0x04, 0x05, 0xF7 };

	struct ble_midi bm = { 0 };

	ck_assert_uint_eq(ble_midi_parse(&bm, data1, sizeof(data1)), 0);
	ck_assert_uint_eq(ble_midi_parse(&bm, data2, sizeof(data2)), 1);
	ck_assert_uint_eq(ble_midi_parse(&bm, data2, sizeof(data2)), 0);

	ck_assert_uint_eq(bm.ts, 0x0001);
	ck_assert_uint_eq(bm.len, sizeof(midi));
	ck_assert_mem_eq(bm.buffer, midi, sizeof(midi));

} CK_END_TEST

CK_START_TEST(test_ble_midi_parse_multiple_system_exclusive_2) {

	const uint8_t data1[] = { 0x80, 0x81, 0xF0, 0x01, 0x02, 0x03 };
	const uint8_t data2[] = { 0x80, 0x82, 0xF7 };
	const uint8_t midi[] = { 0xF0, 0x01, 0x02, 0x03, 0xF7 };

	struct ble_midi bm = { 0 };

	ck_assert_uint_eq(ble_midi_parse(&bm, data1, sizeof(data1)), 0);
	ck_assert_uint_eq(ble_midi_parse(&bm, data2, sizeof(data2)), 1);
	ck_assert_uint_eq(ble_midi_parse(&bm, data2, sizeof(data2)), 0);

	ck_assert_uint_eq(bm.ts, 0x0001);
	ck_assert_uint_eq(bm.len, sizeof(midi));
	ck_assert_mem_eq(bm.buffer, midi, sizeof(midi));

} CK_END_TEST

CK_START_TEST(test_ble_midi_parse_multiple_system_exclusive_3) {

	struct ble_midi bm = { 0 };

	const uint8_t data1[] = { 0x80, 0x81, 0xF0, 0x01, 0x02, 0x03 };
	uint8_t data2[512] = { 0x80, 0x81, 0x77 };
	memset(data2 + 3, 0x77, sizeof(data2) - 3);
	const uint8_t data3[] = { 0x80, 0x82, 0xF7 };
	uint8_t midi[sizeof(bm.buffer_sys)] = { 0xF0, 0x01, 0x02, 0x03, 0x77 };
	memset(midi + 5, 0x77, sizeof(midi) - 5);

	ck_assert_uint_eq(ble_midi_parse(&bm, data1, sizeof(data1)), 0);
	ck_assert_uint_eq(ble_midi_parse(&bm, data2, sizeof(data2)), 0);
	ck_assert_uint_eq(ble_midi_parse(&bm, data3, sizeof(data3)), 1);
	ck_assert_uint_eq(ble_midi_parse(&bm, data3, sizeof(data3)), 0);

	ck_assert_uint_eq(bm.ts, 0x0001);
	ck_assert_uint_eq(bm.len, sizeof(midi));
	ck_assert_mem_eq(bm.buffer, midi, sizeof(midi));
	ck_assert_uint_eq(errno, EMSGSIZE);

} CK_END_TEST

CK_START_TEST(test_ble_midi_parse_invalid_system_exclusive) {

	const uint8_t data[] = { 0x80, 0x80, 0xF0, 0x01, 0x80 };

	struct ble_midi bm = { 0 };
	ck_assert_uint_eq(ble_midi_parse(&bm, data, sizeof(data)), -1);

} CK_END_TEST

CK_START_TEST(test_ble_midi_parse_single_running_status) {

	/* Data:
	 * - full MIDI message (note on)
	 * - running status MIDI message with timestamp byte
	 * - running status MIDI message without timestamp byte */
	const uint8_t data[] = { 0x80, 0x81, 0x90, 0x40, 0x7f, 0x82, 0x41, 0x7f, 0x42, 0x7f };
	const uint8_t midi1[] = { 0x90, 0x40, 0x7f };
	const uint8_t midi2[] = { 0x41, 0x7f };
	const uint8_t midi3[] = { 0x42, 0x7f };

	struct ble_midi bm = { 0 };

	ck_assert_uint_eq(ble_midi_parse(&bm, data, sizeof(data)), 1);
	ck_assert_uint_eq(bm.ts, 0x0001);
	ck_assert_uint_eq(bm.len, sizeof(midi1));
	ck_assert_mem_eq(bm.buffer, midi1, sizeof(midi1));

	ck_assert_uint_eq(ble_midi_parse(&bm, data, sizeof(data)), 1);
	ck_assert_uint_eq(bm.ts, 0x0002);
	ck_assert_uint_eq(bm.len, sizeof(midi2));
	ck_assert_mem_eq(bm.buffer, midi2, sizeof(midi2));

	ck_assert_uint_eq(ble_midi_parse(&bm, data, sizeof(data)), 1);
	ck_assert_uint_eq(bm.ts, 0x0002);
	ck_assert_uint_eq(bm.len, sizeof(midi3));
	ck_assert_mem_eq(bm.buffer, midi3, sizeof(midi3));

} CK_END_TEST

CK_START_TEST(test_ble_midi_parse_single_running_status_with_real_time) {

	/* Data:
	 * - full MIDI message (note on)
	 * - system real-time MIDI message with timestamp byte
	 * - running status MIDI message with timestamp byte */
	const uint8_t data[] = { 0x80, 0x81, 0x90, 0x40, 0x7f, 0x82, 0xF8, 0x83, 0x41, 0x7f };
	const uint8_t midi1[] = { 0x90, 0x40, 0x7f };
	const uint8_t midi2[] = { 0xF8 };
	const uint8_t midi3[] = { 0x41, 0x7f };

	struct ble_midi bm = { 0 };

	ck_assert_uint_eq(ble_midi_parse(&bm, data, sizeof(data)), 1);
	ck_assert_uint_eq(bm.ts, 0x0001);
	ck_assert_uint_eq(bm.len, sizeof(midi1));
	ck_assert_mem_eq(bm.buffer, midi1, sizeof(midi1));

	ck_assert_uint_eq(ble_midi_parse(&bm, data, sizeof(data)), 1);
	ck_assert_uint_eq(bm.ts, 0x0002);
	ck_assert_uint_eq(bm.len, sizeof(midi2));
	ck_assert_mem_eq(bm.buffer, midi2, sizeof(midi2));

	ck_assert_uint_eq(ble_midi_parse(&bm, data, sizeof(data)), 1);
	ck_assert_uint_eq(bm.ts, 0x0003);
	ck_assert_uint_eq(bm.len, sizeof(midi3));
	ck_assert_mem_eq(bm.buffer, midi3, sizeof(midi3));

} CK_END_TEST

CK_START_TEST(test_ble_midi_parse_single_running_status_with_common) {

	/* Data:
	 * - full MIDI message (note on)
	 * - system common MIDI message with timestamp byte
	 * - running status MIDI message with timestamp byte */
	const uint8_t data[] = { 0x80, 0x81, 0x90, 0x40, 0x7f, 0x82, 0xF1, 0x00, 0x83, 0x41, 0x7f };
	const uint8_t midi1[] = { 0x90, 0x40, 0x7f };
	const uint8_t midi2[] = { 0xF1, 0x00 };
	const uint8_t midi3[] = { 0x90, 0x41, 0x7f };

	struct ble_midi bm = { 0 };

	ck_assert_uint_eq(ble_midi_parse(&bm, data, sizeof(data)), 1);
	ck_assert_uint_eq(bm.ts, 0x0001);
	ck_assert_uint_eq(bm.len, sizeof(midi1));
	ck_assert_mem_eq(bm.buffer, midi1, sizeof(midi1));

	ck_assert_uint_eq(ble_midi_parse(&bm, data, sizeof(data)), 1);
	ck_assert_uint_eq(bm.ts, 0x0002);
	ck_assert_uint_eq(bm.len, sizeof(midi2));
	ck_assert_mem_eq(bm.buffer, midi2, sizeof(midi2));

	ck_assert_uint_eq(ble_midi_parse(&bm, data, sizeof(data)), 1);
	ck_assert_uint_eq(bm.ts, 0x0003);
	ck_assert_uint_eq(bm.len, sizeof(midi3));
	ck_assert_mem_eq(bm.buffer, midi3, sizeof(midi3));

} CK_END_TEST

CK_START_TEST(test_ble_midi_parse_multiple_running_status) {

	const uint8_t data1[] = { 0x80, 0x81, 0x90, 0x40, 0x7f };
	const uint8_t data2[] = { 0x80, 0x82, 0x41, 0x7f };
	const uint8_t data3[] = { 0x80, 0x42, 0x7f };
	const uint8_t midi1[] = { 0x90, 0x40, 0x7f };
	const uint8_t midi2[] = { 0x41, 0x7f };
	const uint8_t midi3[] = { 0x42, 0x7f };

	struct ble_midi bm = { 0 };

	ck_assert_uint_eq(ble_midi_parse(&bm, data1, sizeof(data1)), 1);
	ck_assert_uint_eq(ble_midi_parse(&bm, data1, sizeof(data1)), 0);
	ck_assert_uint_eq(bm.ts, 0x0001);
	ck_assert_uint_eq(bm.len, sizeof(midi1));
	ck_assert_mem_eq(bm.buffer, midi1, sizeof(midi1));

	ck_assert_uint_eq(ble_midi_parse(&bm, data2, sizeof(data2)), 1);
	ck_assert_uint_eq(ble_midi_parse(&bm, data2, sizeof(data2)), 0);
	ck_assert_uint_eq(bm.ts, 0x0002);
	ck_assert_uint_eq(bm.len, sizeof(midi2));
	ck_assert_mem_eq(bm.buffer, midi2, sizeof(midi2));

	ck_assert_uint_eq(ble_midi_parse(&bm, data3, sizeof(data3)), 1);
	ck_assert_uint_eq(ble_midi_parse(&bm, data3, sizeof(data3)), 0);
	ck_assert_uint_eq(bm.ts, 0x0002);
	ck_assert_uint_eq(bm.len, sizeof(midi3));
	ck_assert_mem_eq(bm.buffer, midi3, sizeof(midi3));

} CK_END_TEST

int main(void) {

	Suite *s = suite_create(__FILE__);
	TCase *tc = tcase_create(__FILE__);
	SRunner *sr = srunner_create(s);

	suite_add_tcase(s, tc);

	tcase_add_test(tc, test_ble_midi_parse_single);
	tcase_add_test(tc, test_ble_midi_parse_multiple);
	tcase_add_test(tc, test_ble_midi_parse_invalid_header);
	tcase_add_test(tc, test_ble_midi_parse_invalid_status);
	tcase_add_test(tc, test_ble_midi_parse_invalid_interleaved_real_time);
	tcase_add_test(tc, test_ble_midi_parse_single_joined);
	tcase_add_test(tc, test_ble_midi_parse_single_real_time);
	tcase_add_test(tc, test_ble_midi_parse_multiple_real_time);
	tcase_add_test(tc, test_ble_midi_parse_single_system_exclusive);
	tcase_add_test(tc, test_ble_midi_parse_multiple_system_exclusive);
	tcase_add_test(tc, test_ble_midi_parse_multiple_system_exclusive_2);
	tcase_add_test(tc, test_ble_midi_parse_multiple_system_exclusive_3);
	tcase_add_test(tc, test_ble_midi_parse_invalid_system_exclusive);
	tcase_add_test(tc, test_ble_midi_parse_single_running_status);
	tcase_add_test(tc, test_ble_midi_parse_single_running_status_with_real_time);
	tcase_add_test(tc, test_ble_midi_parse_single_running_status_with_common);
	tcase_add_test(tc, test_ble_midi_parse_multiple_running_status);

	srunner_run_all(sr, CK_ENV);
	int nf = srunner_ntests_failed(sr);
	srunner_free(sr);

	return nf == 0 ? 0 : 1;
}
