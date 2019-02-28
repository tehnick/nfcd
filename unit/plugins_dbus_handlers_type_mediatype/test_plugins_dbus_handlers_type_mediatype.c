/*
 * Copyright (C) 2018 Jolla Ltd.
 * Copyright (C) 2018 Slava Monich <slava.monich@jolla.com>
 *
 * You may use this file under the terms of BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the names of the copyright holders nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "dbus_handlers/dbus_handlers.h"

#include "test_common.h"

#include <nfc_ndef.h>

#include <glib/gstdio.h>

static TestOpt test_opt;

static
NfcNdefRec*
test_ndef_record_new(
    const char* mediatype,
    const GUtilData* payload)
{
    GUtilData data;
    NfcNdefRec* rec;
    const gsize type_length = strlen(mediatype);
    const gsize payload_length = (payload ? payload->size : 0);
    guint8* bytes;

    g_assert(type_length < 0x100);
    g_assert(payload_length < 0x100);
    data.size = 3 + type_length + (payload ? payload->size : 0);
    data.bytes = bytes = g_malloc(data.size);
    bytes[0] = 0xd2;                   /* (MB,ME,SR,TNF=0x02) */
    bytes[1] = (guint8)type_length;    /* Length of the record type */
    bytes[2] = (guint8)payload_length; /* Length of the record payload */
    memcpy(bytes + 3, mediatype, type_length);
    if (payload_length) {
        memcpy(bytes + 3 + type_length, payload->bytes, payload_length);
    }

    rec = nfc_ndef_rec_new(&data);
    g_assert(rec);
    g_free(bytes);
    return rec;
}

static
NfcNdefRec*
test_ndef_record_new_text(
    const char* mediatype,
    const char* text)
{
    GUtilData payload;

    if (text) {
        payload.bytes = (void*)text;
        payload.size = strlen(text);
    } else {
        memset(&payload, 0, sizeof(payload));
    }
    return test_ndef_record_new(mediatype, &payload);
}

/*==========================================================================*
 * recognize
 *==========================================================================*/

static
void
test_recognize(
    void)
{
    GUtilData bytes;
    NfcNdefRec* rec;
    static const guint8 ndef_data[] = {
        0xd1,       /* NDEF record header (MB,ME,SR,TNF=0x01) */
        0x01,       /* Length of the record type */
        0x00,       /* Length of the record payload */
        'x'         /* Record type: 'x' */
    };

    g_assert(!dbus_handlers_type_mediatype_record(NULL));

    /* Not a media-type record */
    TEST_BYTES_SET(bytes, ndef_data);
    rec = nfc_ndef_rec_new(&bytes);
    g_assert(rec);
    g_assert(!dbus_handlers_type_mediatype_record(rec));
    nfc_ndef_rec_unref(rec);

    /* Invalid media types */
    rec = test_ndef_record_new("", NULL);
    g_assert(!dbus_handlers_type_mediatype_record(rec));
    nfc_ndef_rec_unref(rec);

    rec = test_ndef_record_new(" ", NULL);
    g_assert(!dbus_handlers_type_mediatype_record(rec));
    nfc_ndef_rec_unref(rec);

    rec = test_ndef_record_new("foo", NULL);
    g_assert(!dbus_handlers_type_mediatype_record(rec));
    nfc_ndef_rec_unref(rec);

    rec = test_ndef_record_new("*", NULL);
    g_assert(!dbus_handlers_type_mediatype_record(rec));
    nfc_ndef_rec_unref(rec);

    rec = test_ndef_record_new("*/*", NULL);
    g_assert(!dbus_handlers_type_mediatype_record(rec));
    nfc_ndef_rec_unref(rec);

    rec = test_ndef_record_new("foo/", NULL);
    g_assert(!dbus_handlers_type_mediatype_record(rec));
    nfc_ndef_rec_unref(rec);

    rec = test_ndef_record_new("foo ", NULL);
    g_assert(!dbus_handlers_type_mediatype_record(rec));
    nfc_ndef_rec_unref(rec);

    rec = test_ndef_record_new("foo  ", NULL);
    g_assert(!dbus_handlers_type_mediatype_record(rec));
    nfc_ndef_rec_unref(rec);

    rec = test_ndef_record_new("foo/\x80", NULL);
    g_assert(!dbus_handlers_type_mediatype_record(rec));
    nfc_ndef_rec_unref(rec);

    rec = test_ndef_record_new("foo/*", NULL);
    g_assert(!dbus_handlers_type_mediatype_record(rec));
    nfc_ndef_rec_unref(rec);

    rec = test_ndef_record_new("foo/bar\t", NULL);
    g_assert(!dbus_handlers_type_mediatype_record(rec));
    nfc_ndef_rec_unref(rec);

    /* And finally a valid one */
    rec = test_ndef_record_new("foo/bar", NULL);
    g_assert(dbus_handlers_type_mediatype_record(rec));
    nfc_ndef_rec_unref(rec);
}

/*==========================================================================*
 * basic
 *==========================================================================*/

static
void
test_basic(
    void)
{
    GVariant* args;
    char* dir = g_dir_make_tmp("test_XXXXXX", NULL);
    char* fname1 = g_build_filename(dir, "test1.conf", NULL);
    char* fname2 = g_build_filename(dir, "test2.conf", NULL);
    char* fname3 = g_build_filename(dir, "test3.conf", NULL);
    char* fname4 = g_build_filename(dir, "test4.conf", NULL);
    char* fname5 = g_build_filename(dir, "test5.conf", NULL);
    NfcNdefRec* rec = test_ndef_record_new_text("text/plain", "test");
    DBusHandlersConfig* handlers;
    const char* contents1 =
        "[MediaType-Handler]\n"
        "MediaType = */*\n"
        "Path = /h1\n"
        "Service = h1.s\n"
        "Method = h1.m\n";
    const char* contents2 =
        "[MediaType-Handler]\n"
        "MediaType = text/plain\n"
        "Path = /h2\n"
        "Service = h2.s\n"
        "Method = h2.m\n";
    const char* contents3 =
        "[MediaType-Listener]\n"
        "MediaType = text/*\n"
        "Path = /l1\n"
        "Service = l1.s\n"
        "Method = l1.m\n";
    const char* contents4 =
        "[MediaType-Listener]\n"
        "MediaType = text/plain\n"
        "Path = /l2\n"
        "Service = l2.s\n"
        "Method = l2.m\n";
    const char* contents5 =
        "[MediaType-Listener]\n"
        "MediaType = image/jpeg\n"
        "Path = /l3\n"
        "Service = l3.s\n"
        "Method = l3.m\n";

    GDEBUG("created %s", dir);
    g_assert(g_file_set_contents(fname1, contents1, -1, NULL));
    g_assert(g_file_set_contents(fname2, contents2, -1, NULL));
    g_assert(g_file_set_contents(fname3, contents3, -1, NULL));
    g_assert(g_file_set_contents(fname4, contents4, -1, NULL));
    g_assert(g_file_set_contents(fname5, contents5, -1, NULL));

    g_assert(rec);
    handlers = dbus_handlers_config_load(dir, rec);
    g_unlink(fname1);
    g_unlink(fname2);
    g_unlink(fname3);
    g_unlink(fname4);
    g_unlink(fname5);
    g_rmdir(dir);

    g_assert(handlers);
    g_assert(handlers->handlers);
    g_assert(handlers->handlers->next);
    g_assert(!handlers->handlers->next->next);
    g_assert(handlers);
    g_assert(handlers->listeners);
    g_assert(handlers->listeners->next);
    g_assert(!handlers->listeners->next->next);

    g_assert(!g_strcmp0(handlers->handlers->dbus.service, "h2.s"));
    g_assert(!g_strcmp0(handlers->handlers->dbus.path, "/h2"));
    g_assert(!g_strcmp0(handlers->handlers->next->dbus.service, "h1.s"));
    g_assert(!g_strcmp0(handlers->handlers->next->dbus.path, "/h1"));
    g_assert(!g_strcmp0(handlers->listeners->dbus.service, "l2.s"));
    g_assert(!g_strcmp0(handlers->listeners->dbus.path, "/l2"));
    g_assert(!g_strcmp0(handlers->listeners->next->dbus.service, "l1.s"));
    g_assert(!g_strcmp0(handlers->listeners->next->dbus.path, "/l1"));

    args = handlers->handlers->type->handler_args(rec);
    g_assert(args);
    g_assert(!g_strcmp0(g_variant_get_type_string(args), "(say)"));
    g_variant_unref(g_variant_ref_sink(args));

    args = handlers->handlers->type->listener_args(TRUE, rec);
    g_assert(args);
    g_assert(!g_strcmp0(g_variant_get_type_string(args), "(bsay)"));
    g_variant_unref(g_variant_ref_sink(args));

    /* Try empty record too */
    nfc_ndef_rec_unref(rec);
    rec = test_ndef_record_new("", NULL);

    args = handlers->handlers->type->handler_args(rec);
    g_assert(args);
    g_assert(!g_strcmp0(g_variant_get_type_string(args), "(say)"));
    g_variant_unref(g_variant_ref_sink(args));

    args = handlers->handlers->type->listener_args(TRUE, rec);
    g_assert(args);
    g_assert(!g_strcmp0(g_variant_get_type_string(args), "(bsay)"));
    g_variant_unref(g_variant_ref_sink(args));

    dbus_handlers_config_free(handlers);
    nfc_ndef_rec_unref(rec);
    g_free(fname1);
    g_free(fname2);
    g_free(fname3);
    g_free(fname4);
    g_free(fname5);
    g_free(dir);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/plugins/dbus_handlers/type_mediatype/" name

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("recognize"), test_recognize);
    g_test_add_func(TEST_("basic"), test_basic);
    test_init(&test_opt, argc, argv);
    return g_test_run();
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */