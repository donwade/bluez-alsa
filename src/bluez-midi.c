/*
 * BlueALSA - bluez-midi.c
 * Copyright (c) 2016-2023 Arkadiusz Bokowy
 *
 * This file is a part of bluez-alsa.
 *
 * This project is licensed under the terms of the MIT license.
 *
 */

#include "bluez-midi.h"

#if HAVE_CONFIG_H
# include <config.h>
#endif

#include <errno.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>
#include <gio/gunixfdlist.h>
#include <glib-object.h>
#include <glib.h>

#include <bluetooth/bluetooth.h> /* IWYU pragma: keep */
#include <bluetooth/hci.h>

#include "ba-adapter.h"
#include "ba-device.h"
#include "ba-transport.h"
#include "bluealsa-config.h"
#include "bluez-iface.h"
#include "dbus.h"
#include "shared/bluetooth.h"
#include "shared/defs.h"
#include "shared/log.h"

/**
 * BlueALSA MIDI GATT application. */
struct bluez_midi_app {
	/* D-Bus object registration paths */
	char path[64];
	char path_service[64 + 8];
	char path_char[64 + 16];
	/* associated adapter */
	int hci_dev_id;
	/* associated transport */
	struct ba_transport *t;
	/* GATT write/notify acquisition */
	bool acquired_write;
	bool acquired_notify;
	/* memory self-management */
	atomic_int ref_count;
};

static GVariant *variant_new_midi_service_uuid(void) {
	return g_variant_new_string(BT_UUID_MIDI);
}

static GVariant *variant_new_midi_characteristic_uuid(void) {
	return g_variant_new_string(BT_UUID_MIDI_CHAR);
}

static void bluez_midi_app_unref(struct bluez_midi_app *app) {
	if (atomic_fetch_sub_explicit(&app->ref_count, 1, memory_order_relaxed) > 1)
		return;
	if (app->t != NULL)
		ba_transport_destroy(app->t);
	free(app);
}

/**
 * Create new local MIDI transport.
 *
 * Unfortunately, BlueZ doesn't provide any meaningful information about the
 * remote device which wants to acquire the write/notify access. There is a
 * "device" option, but the acquire-write and acquire-notify methods are called
 * only for the first device, and the application (us) is not notified when
 * some other device wants to acquire the access. Therefore, from our point of
 * view, we can tell only that there will be an incoming connection from given
 * adapter. */
static struct ba_transport *bluez_midi_transport_new(
		struct bluez_midi_app *app) {

	struct ba_adapter *a = NULL;
	struct ba_device *d = NULL;
	struct ba_transport *t = NULL;

	if ((a = ba_adapter_lookup(app->hci_dev_id)) == NULL) {
		error("Couldn't lookup adapter: hci%d: %s", app->hci_dev_id, strerror(errno));
		goto fail;
	}

	if ((d = ba_device_lookup(a, &a->hci.bdaddr)) == NULL &&
			(d = ba_device_new(a, &a->hci.bdaddr)) == NULL) {
		error("Couldn't create new device: %s", strerror(errno));
		goto fail;
	}

	if ((t = ba_transport_lookup(d, app->path)) == NULL &&
			(t = ba_transport_new_midi(d, BA_TRANSPORT_PROFILE_MIDI, ":0", app->path)) == NULL) {
		error("Couldn't create new transport: %s", strerror(errno));
		goto fail;
	}

fail:
	if (a != NULL)
		ba_adapter_unref(a);
	if (d != NULL)
		ba_device_unref(d);
	return t;
}

/**
 * Lookup for existing local MIDI transport. */
static struct ba_transport *bluez_midi_transport_lookup(
		struct bluez_midi_app *app) {

	struct ba_adapter *a = NULL;
	struct ba_device *d = NULL;
	struct ba_transport *t = NULL;

	if ((a = ba_adapter_lookup(app->hci_dev_id)) == NULL) {
		error("Couldn't lookup adapter: hci%d: %s", app->hci_dev_id, strerror(errno));
		goto fail;
	}

	if ((d = ba_device_lookup(a, &a->hci.bdaddr)) == NULL) {
		error("Couldn't lookup local device: %s", strerror(errno));
		goto fail;
	}

	if ((t = ba_transport_lookup(d, app->path)) == NULL) {
		error("Couldn't lookup local device MIDI transport: %s", strerror(errno));
		goto fail;
	}

fail:
	if (a != NULL)
		ba_adapter_unref(a);
	if (d != NULL)
		ba_device_unref(d);
	return t;
}

static GVariant *bluez_midi_service_iface_get_property(
		const char *property, G_GNUC_UNUSED GError **error,
		G_GNUC_UNUSED void *userdata) {

	if (strcmp(property, "UUID") == 0)
		return variant_new_midi_service_uuid();
	if (strcmp(property, "Primary") == 0)
		return g_variant_new_boolean(TRUE);

	g_assert_not_reached();
	return NULL;
}

static GDBusObjectSkeleton *bluez_midi_service_skeleton_new(
		struct bluez_midi_app *app) {

	static const GDBusInterfaceSkeletonVTable vtable = {
		.get_property = bluez_midi_service_iface_get_property,
	};

	OrgBluezGattService1Skeleton *ifs_gatt_service;
	if ((ifs_gatt_service = org_bluez_gatt_service1_skeleton_new(&vtable,
					app, (GDestroyNotify)bluez_midi_app_unref)) == NULL)
		return NULL;

	GDBusInterfaceSkeleton *ifs = G_DBUS_INTERFACE_SKELETON(ifs_gatt_service);
	GDBusObjectSkeleton *skeleton = g_dbus_object_skeleton_new(app->path_service);
	g_dbus_object_skeleton_add_interface(skeleton, ifs);
	g_object_unref(ifs_gatt_service);

	atomic_fetch_add_explicit(&app->ref_count, 1, memory_order_relaxed);
	return skeleton;
}

static void bluez_midi_characteristic_read_value(
		GDBusMethodInvocation *inv, G_GNUC_UNUSED void *userdata) {
	GVariant *rv[] = { /* respond with no payload */
		g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, NULL, 0, sizeof(uint8_t)) };
	g_dbus_method_invocation_return_value(inv, g_variant_new_tuple(rv, 1));
}

static bool bluez_midi_params_get_mtu(GVariant *params, uint16_t *mtu) {
	GVariant *params_ = g_variant_get_child_value(params, 0);
	bool ok = g_variant_lookup(params_, "mtu", "q", mtu) == TRUE;
	g_variant_unref(params_);
	return ok;
}

static void bluez_midi_characteristic_acquire_write(
		GDBusMethodInvocation *inv, void *userdata) {

	GVariant *params = g_dbus_method_invocation_get_parameters(inv);
	struct bluez_midi_app *app = userdata;

	uint16_t mtu = 0;
	if (!bluez_midi_params_get_mtu(params, &mtu)) {
		error("Couldn't acquire MIDI char write: %s", "Invalid options");
		goto fail;
	}

	struct ba_transport *t = NULL;
	if ((t = bluez_midi_transport_lookup(app)) == NULL)
		goto fail;

	app->acquired_write = true;
	t->mtu_read = mtu;

	GUnixFDList *fd_list = g_unix_fd_list_new();
	g_unix_fd_list_append(fd_list, t->midi.fd, NULL);
	g_dbus_method_invocation_return_value_with_unix_fd_list(inv,
			g_variant_new("(hq)", 0, mtu), fd_list);
	g_object_unref(fd_list);

	return;

fail:
	g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
			G_DBUS_ERROR_INVALID_ARGS, "Unable to acquire write access");
}

static void bluez_midi_characteristic_acquire_notify(
		GDBusMethodInvocation *inv, void *userdata) {

	GVariant *params = g_dbus_method_invocation_get_parameters(inv);
	struct bluez_midi_app *app = userdata;

	uint16_t mtu = 0;
	if (!bluez_midi_params_get_mtu(params, &mtu)) {
		error("Couldn't acquire MIDI char notify: %s", "Invalid options");
		goto fail;
	}

	struct ba_transport *t = NULL;
	if ((t = bluez_midi_transport_lookup(app)) == NULL)
		goto fail;

	app->acquired_notify = true;
	t->mtu_write = mtu;

	GUnixFDList *fd_list = g_unix_fd_list_new();
	g_unix_fd_list_append(fd_list, t->midi.fd, NULL);
	g_dbus_method_invocation_return_value_with_unix_fd_list(inv,
			g_variant_new("(hq)", 0, mtu), fd_list);
	g_object_unref(fd_list);

	return;

fail:
	g_dbus_method_invocation_return_error(inv, G_DBUS_ERROR,
			G_DBUS_ERROR_INVALID_ARGS, "Unable to acquire notification");
}

static GVariant *bluez_midi_characteristic_iface_get_property(
		const char *property, G_GNUC_UNUSED GError **error, void *userdata) {

	struct bluez_midi_app *app = userdata;

	if (strcmp(property, "UUID") == 0)
		return variant_new_midi_characteristic_uuid();
	if (strcmp(property, "Service") == 0)
		return g_variant_new_object_path(app->path_service);
	if (strcmp(property, "WriteAcquired") == 0)
		return g_variant_new_boolean(app->acquired_write);
	if (strcmp(property, "NotifyAcquired") == 0)
		return g_variant_new_boolean(app->acquired_notify);
	if (strcmp(property, "Flags") == 0) {
		const char *values[] = {
			"read", "write", "write-without-response", "notify" };
		return g_variant_new_strv(values, ARRAYSIZE(values));
	}

	g_assert_not_reached();
	return NULL;
}

static GDBusObjectSkeleton *bluez_midi_characteristic_skeleton_new(
		struct bluez_midi_app *app) {

	static const GDBusMethodCallDispatcher dispatchers[] = {
		{ .method = "ReadValue",
			.handler = bluez_midi_characteristic_read_value },
		{ .method = "AcquireWrite",
			.handler = bluez_midi_characteristic_acquire_write },
		{ .method = "AcquireNotify",
			.handler = bluez_midi_characteristic_acquire_notify },
		{ 0 },
	};

	static const GDBusInterfaceSkeletonVTable vtable = {
		.dispatchers = dispatchers,
		.get_property = bluez_midi_characteristic_iface_get_property,
	};

	OrgBluezGattCharacteristic1Skeleton *ifs_gatt_char;
	if ((ifs_gatt_char = org_bluez_gatt_characteristic1_skeleton_new(&vtable,
					app, (GDestroyNotify)bluez_midi_app_unref)) == NULL)
		return NULL;

	GDBusInterfaceSkeleton *ifs = G_DBUS_INTERFACE_SKELETON(ifs_gatt_char);
	GDBusObjectSkeleton *skeleton = g_dbus_object_skeleton_new(app->path_char);
	g_dbus_object_skeleton_add_interface(skeleton, ifs);
	g_object_unref(ifs_gatt_char);

	atomic_fetch_add_explicit(&app->ref_count, 1, memory_order_relaxed);
	return skeleton;
}

static void bluez_midi_app_register_finish(
		GObject *source, GAsyncResult *result, G_GNUC_UNUSED void *userdata) {

	GError *err = NULL;
	GDBusMessage *rep = g_dbus_connection_send_message_with_reply_finish(
			G_DBUS_CONNECTION(source), result, &err);
	if (rep != NULL &&
			g_dbus_message_get_message_type(rep) == G_DBUS_MESSAGE_TYPE_ERROR)
		g_dbus_message_to_gerror(rep, &err);

	if (rep != NULL)
		g_object_unref(rep);
	if (err != NULL) {
		error("Couldn't register MIDI GATT application: %s", err->message);
		g_error_free(err);
	}

}

GDBusObjectManagerServer *bluez_midi_app_new(
		struct ba_adapter *adapter, const char *path) {

	struct bluez_midi_app *app;
	if ((app = calloc(1, sizeof(*app))) == NULL)
		return NULL;

	snprintf(app->path, sizeof(app->path), "%s", path);
	snprintf(app->path_service, sizeof(app->path_service), "%s/service", path);
	snprintf(app->path_char, sizeof(app->path_char), "%s/char", app->path_service);
	app->hci_dev_id = adapter->hci.dev_id;

	GDBusObjectManagerServer *manager = g_dbus_object_manager_server_new(path);
	GDBusObjectSkeleton *skeleton;

	skeleton = bluez_midi_service_skeleton_new(app);
	g_dbus_object_manager_server_export(manager, skeleton);
	g_object_unref(skeleton);

	skeleton = bluez_midi_characteristic_skeleton_new(app);
	g_dbus_object_manager_server_export(manager, skeleton);
	g_object_unref(skeleton);

	g_dbus_object_manager_server_set_connection(manager, config.dbus);

	GDBusMessage *msg;
	msg = g_dbus_message_new_method_call(BLUEZ_SERVICE, adapter->bluez_dbus_path,
			BLUEZ_IFACE_GATT_MANAGER, "RegisterApplication");

	g_dbus_message_set_body(msg, g_variant_new("(oa{sv})", path, NULL));

	debug("Registering MIDI GATT application: %s", app->path);
	g_dbus_connection_send_message_with_reply(config.dbus, msg,
			G_DBUS_SEND_MESSAGE_FLAGS_NONE, -1, NULL, NULL,
			bluez_midi_app_register_finish, NULL);

	struct ba_transport *t;
	/* Setup local MIDI transport associated with our GATT server. */
	if ((t = bluez_midi_transport_new(app)) == NULL)
		error("Couldn't create local MIDI transport: %s", strerror(errno));
	else if (ba_transport_acquire(t) == -1)
		error("Couldn't acquire local MIDI transport: %s", strerror(errno));
	app->t = t;

	g_object_unref(msg);
	return manager;
}
