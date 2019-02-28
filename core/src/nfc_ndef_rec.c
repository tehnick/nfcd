/*
 * Copyright (C) 2018-2019 Jolla Ltd.
 * Copyright (C) 2018-2019 Slava Monich <slava.monich@jolla.com>
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
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
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

#include "nfc_ndef_p.h"
#include "nfc_util.h"
#include "nfc_tlv.h"
#include "nfc_log.h"

#include <gutil_misc.h>

struct nfc_ndef_rec_priv {
    void* data;
};

G_DEFINE_TYPE(NfcNdefRec, nfc_ndef_rec, G_TYPE_OBJECT)

static const GUtilData nfc_ndef_rec_type_sp = { (const guint8*) "Sp", 2 };
static const GUtilData nfc_ndef_rec_type_hs = { (const guint8*) "Hs", 2 };
static const GUtilData nfc_ndef_rec_type_hr = { (const guint8*) "Hr", 2 };
static const GUtilData nfc_ndef_rec_type_hc = { (const guint8*) "Hc", 2 };
static const GUtilData nfc_ndef_rec_type_ac = { (const guint8*) "ac", 2 };
static const GUtilData nfc_ndef_rec_type_cr = { (const guint8*) "cr", 2 };
static const GUtilData nfc_ndef_rec_type_err = { (const guint8*) "err", 3 };

static
NfcNdefRec*
nfc_ndef_rec_alloc(
    const NfcNdefData* ndef)
{
    if (ndef->rec.size) {
        GUtilData type;

        /* Handle known types */
        nfc_ndef_type(ndef, &type);
        if (gutil_data_equal(&type, &nfc_ndef_rec_type_u)) {
            NfcNdefRecU* uri_rec = nfc_ndef_rec_u_new_from_data(ndef);

            if (uri_rec) {
                /* URI Record */
                GDEBUG("URI Record: %s", uri_rec->uri);
                return NFC_NDEF_REC(uri_rec);
            }
        } else if (gutil_data_equal(&type, &nfc_ndef_rec_type_t)) {
            NfcNdefRecT* text_rec = nfc_ndef_rec_t_new_from_data(ndef);

            if (text_rec) {
                /* TEXT Record */
                GVERBOSE("Text Record Language: %s", text_rec->lang);
                GDEBUG("Text Record: %s", text_rec->text);
                return NFC_NDEF_REC(text_rec);
            }
        }

        /* Generic record */
        return nfc_ndef_rec_initialize(g_object_new(NFC_TYPE_NDEF_REC, NULL),
            NFC_NDEF_RTD_UNKNOWN, ndef);
    } else {
        /* Special case - Empty NDEF */
        return g_object_new(NFC_TYPE_NDEF_REC, NULL);
    }
}

static
gboolean
nfc_ndef_rec_parse(
    GUtilData* block,
    NfcNdefData* ndef)
{
    if (block->size < 3) {
        /* At least 3 bytes is required for anything meaningful */
        GDEBUG("Block is too short to be an NDEF record");
        return FALSE;
    } else {
        const guint8 hdr = block->bytes[0];
        guint total_len = 1;

        memset(ndef, 0, sizeof(*ndef));
        ndef->type_length = block->bytes[1];

        /* Type */
        total_len += 1 + ndef->type_length;
        ndef->type_offset = 2;

        /* Payload length */
        if (hdr & NFC_NDEF_HDR_SR) {
            /* Short record */
            ndef->payload_length = block->bytes[ndef->type_offset++];
            total_len += 1 + ndef->payload_length;
        } else {
            /* 4 bytes for length */
            ndef->payload_length =
                (((guint)block->bytes[ndef->type_offset]) << 24) |
                (((guint)block->bytes[ndef->type_offset + 1]) << 16) |
                (((guint)block->bytes[ndef->type_offset + 2]) << 8) |
                ((guint)block->bytes[ndef->type_offset + 3]);
            total_len += 4 + ndef->payload_length;
            ndef->type_offset += 4;
        }

        /* ID Length */
        if (hdr & NFC_NDEF_HDR_IL) {
            ndef->id_length = block->bytes[ndef->type_offset++];
            total_len += 1 + ndef->id_length;
        }

        /* Check for overflow */
        if (ndef->payload_length < 0x80000000 && total_len <= block->size) {
            /* Cut the garbage if there is any */
            ndef->rec.bytes = block->bytes;
            ndef->rec.size = total_len;
            block->bytes += total_len;
            block->size -= total_len;
            return TRUE;
        } else {
            GDEBUG("Garbage (lengths don't add up)");
        }
        return FALSE;
    }
}

/*==========================================================================*
 * Interface
 *==========================================================================*/

NfcNdefRec*
nfc_ndef_rec_new(
   const GUtilData* block)
{
    NfcNdefRec* first = NULL;

    if (G_LIKELY(block)) {
        NfcNdefData ndef;

        memset(&ndef, 0, sizeof(ndef));
        if (G_LIKELY(block->size)) {
            GUtilData data = *block;
            NfcNdefRec* last = NULL;

            while (data.size > 0 && nfc_ndef_rec_parse(&data, &ndef)) {
                GASSERT(ndef.rec.size);
                if (ndef.rec.bytes[0] & NFC_NDEF_HDR_CF) {
                    /* Who needs those anyway? */
                    GWARN("Chunked records are not supported");
                } else {
                    NfcNdefRec* rec;

                    GDEBUG("NDEF:");
                    nfc_hexdump_data(&ndef.rec);
                    rec = nfc_ndef_rec_alloc(&ndef);
                    if (last) {
                        last->next = rec;
                        last = rec;
                    } else {
                        first = last = rec;
                    }
                }
            }
        } else {
            /* Special case - Empty NDEF */
            GDEBUG("Empty NDEF");
            first = nfc_ndef_rec_alloc(&ndef);
        }
    }
    return first;
}

NfcNdefRec*
nfc_ndef_rec_new_tlv(
    const GUtilData* tlv)
{
    NfcNdefRec* first = NULL;

    if (G_LIKELY(tlv)) {
        GUtilData buf = *tlv, value;
        NfcNdefRec* last = NULL;
        guint type;

        while ((type = nfc_tlv_next(&buf, &value)) > 0) {
            if (type == TLV_NDEF_MESSAGE) {
                NfcNdefRec* rec = nfc_ndef_rec_new(&value);

                if (rec) {
                    if (last) {
                        last->next = rec;
                    } else {
                        first = rec;
                    }
                    /* nfc_ndef_rec_new() can return a chain */
                    last = rec;
                    while (last->next) {
                        last = last->next;
                    }
                }
            }
        }
    }
    return first;
}

NfcNdefRec*
nfc_ndef_rec_ref(
    NfcNdefRec* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(NFC_NDEF_REC(self));
    }
    return self;
}

void
nfc_ndef_rec_unref(
    NfcNdefRec* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(NFC_NDEF_REC(self));
    }
}

/*==========================================================================*
 * Internal interface
 *==========================================================================*/

gboolean
nfc_ndef_type(
    const NfcNdefData* ndef,
    GUtilData* type)
{
    if (ndef && ndef->type_length) {
        type->bytes = ndef->rec.bytes + ndef->type_offset;
        type->size = ndef->type_length;
        return TRUE;
    } else {
        type->bytes = NULL;
        type->size = 0;
        return FALSE;
    }
}

gboolean
nfc_ndef_payload(
    const NfcNdefData* ndef,
    GUtilData* payload)
{
    if (ndef && ndef->payload_length) {
        payload->bytes = ndef->rec.bytes + ndef->type_offset +
            ndef->type_length + ndef->id_length;
        payload->size = ndef->payload_length;
        return TRUE;
    } else {
        payload->bytes = NULL;
        payload->size = 0;
        return FALSE;
    }
}

NfcNdefRec*
nfc_ndef_rec_new_well_known(
    GType gtype,
    NFC_NDEF_RTD rtd,
    const GUtilData* type,
    const GUtilData* payload)
{
    NfcNdefData ndef;
    NfcNdefRec* rec;
    GByteArray* buf = g_byte_array_new();
    guint8 hdr = NFC_NDEF_HDR_MB | NFC_NDEF_HDR_ME | NFC_NDEF_TNF_WELL_KNOWN;
    const guint8 type_len = type->size;

    memset(&ndef, 0, sizeof(ndef));
    ndef.type_length = type->size;
    ndef.payload_length = payload->size;

    /* Header, TYPE LENGTH and PAYLOAD LENGTH */
    if (payload->size > 0xff) {
        guint8 payload_len[4];

        payload_len[0] = (guint8)(payload->size >> 24);
        payload_len[1] = (guint8)(payload->size >> 16);
        payload_len[2] = (guint8)(payload->size >> 8);
        payload_len[3] = (guint8)payload->size;
        g_byte_array_append(buf, &hdr, 1);
        g_byte_array_append(buf, &type_len, 1);
        g_byte_array_append(buf, payload_len, 4);
    } else {
        guint8 payload_len = (guint8)payload->size;

        hdr |= NFC_NDEF_HDR_SR;
        g_byte_array_append(buf, &hdr, 1);
        g_byte_array_append(buf, &type_len, 1);
        g_byte_array_append(buf, &payload_len, 1);
    }

    /* TYPE */
    ndef.type_offset = buf->len;
    g_byte_array_append(buf, type->bytes, type->size);

    /* PAYLOAD */
    g_byte_array_append(buf, payload->bytes, payload->size);

    /* Allocate the object */
    ndef.rec.bytes = buf->data;
    ndef.rec.size = buf->len;
    rec = nfc_ndef_rec_initialize(g_object_new(gtype, NULL), rtd, &ndef);

    g_byte_array_free(buf, TRUE);
    return rec;
}

NfcNdefRec*
nfc_ndef_rec_initialize(
    NfcNdefRec* self,
    NFC_NDEF_RTD rtd,
    const NfcNdefData* ndef)
{
    if (self && ndef) {
        NfcNdefRecPriv* priv = self->priv;
        const GUtilData* rec = &ndef->rec;
        const guint hdr = rec->bytes[0];
        const guint8 tnf = (hdr & NFC_NDEF_HDR_TNF_MASK);

        if (tnf < NFC_NDEF_TNF_MAX) {
            self->tnf = tnf;
        }
        if (hdr & NFC_NDEF_HDR_MB) {
            self->flags |= NFC_NDEF_REC_FLAG_FIRST;
        }
        if (hdr & NFC_NDEF_HDR_ME) {
            self->flags |= NFC_NDEF_REC_FLAG_LAST;
        }
        self->rtd = rtd;
        self->raw.bytes = priv->data = g_memdup(rec->bytes, rec->size);
        self->raw.size = rec->size;
        self->type.bytes = self->raw.bytes + ndef->type_offset;
        self->type.size = ndef->type_length;
        if (ndef->id_length > 0) {
            self->id.bytes = self->type.bytes + ndef->type_length;
            self->id.size = ndef->id_length;
        }
        if (ndef->payload_length) {
            self->payload.size = ndef->payload_length;
            self->payload.bytes = self->type.bytes + ndef->type_length +
                ndef->id_length;
        }
    }
    return self;
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

static
void
nfc_ndef_rec_init(
    NfcNdefRec* self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, NFC_TYPE_NDEF_REC,
        NfcNdefRecPriv);
}

static
void
nfc_ndef_rec_finalize(
    GObject* object)
{
    NfcNdefRec* self = NFC_NDEF_REC(object);
    NfcNdefRecPriv* priv = self->priv;

    g_free(priv->data);
    nfc_ndef_rec_unref(self->next);
    G_OBJECT_CLASS(nfc_ndef_rec_parent_class)->finalize(object);
}

static
void
nfc_ndef_rec_class_init(
    NfcNdefRecClass* klass)
{
    g_type_class_add_private(klass, sizeof(NfcNdefRecPriv));
    G_OBJECT_CLASS(klass)->finalize = nfc_ndef_rec_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */