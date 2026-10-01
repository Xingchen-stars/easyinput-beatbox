#include "beatbox_ble.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "mbedtls/md.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "beatbox_ble";

#define BEATBOX_BLE_DEVICE_NAME "EasyInput Beatbox"
#define BEATBOX_BLE_TRUST_NAMESPACE "beatbox_ble"
#define BEATBOX_BLE_IDENTITY_KEY "own_addr"
#define BEATBOX_BLE_TRUST_LIMIT 3
#define BEATBOX_BLE_CLIENT_NEXT_KEY "client_next"
#define BEATBOX_BLE_CLIENT_ID_BYTES 8
#define BEATBOX_BLE_CLIENT_KEY_BYTES 32
#define BEATBOX_BLE_CHALLENGE_BYTES 16
#define BEATBOX_BLE_PROOF_BYTES 32
#define BEATBOX_BLE_QUEUE_DEPTH 8
#define BEATBOX_BLE_DIAG_QUEUE_DEPTH 16
#define BEATBOX_BLE_FRAGMENT_START 0x01
#define BEATBOX_BLE_FRAGMENT_END 0x02
#define BEATBOX_BLE_FRAGMENT_HEADER 2

/* 8ab6c845-23e4-4f36-91df-8ac820b58101 / 02 / 03, little-endian. */
#define BEATBOX_BLE_SVC_BYTES 0x01, 0x81, 0xb5, 0x20, 0xc8, 0x8a, 0xdf, 0x91, \
                              0x36, 0x4f, 0xe4, 0x23, 0x45, 0xc8, 0xb6, 0x8a
#define BEATBOX_BLE_RX_BYTES  0x02, 0x81, 0xb5, 0x20, 0xc8, 0x8a, 0xdf, 0x91, \
                              0x36, 0x4f, 0xe4, 0x23, 0x45, 0xc8, 0xb6, 0x8a
#define BEATBOX_BLE_TX_BYTES  0x03, 0x81, 0xb5, 0x20, 0xc8, 0x8a, 0xdf, 0x91, \
                              0x36, 0x4f, 0xe4, 0x23, 0x45, 0xc8, 0xb6, 0x8a

typedef struct {
    char line[BEATBOX_BLE_LINE_MAX];
} beatbox_ble_line_t;

typedef struct {
    char line[BEATBOX_BLE_DIAG_LINE_MAX];
} beatbox_ble_diag_line_t;

typedef struct {
    uint8_t id[BEATBOX_BLE_CLIENT_ID_BYTES];
    uint8_t key[BEATBOX_BLE_CLIENT_KEY_BYTES];
} beatbox_ble_client_t;

extern void ble_store_config_init(void);

static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(BEATBOX_BLE_SVC_BYTES);
static const ble_uuid128_t s_rx_uuid = BLE_UUID128_INIT(BEATBOX_BLE_RX_BYTES);
static const ble_uuid128_t s_tx_uuid = BLE_UUID128_INIT(BEATBOX_BLE_TX_BYTES);

static uint16_t s_tx_value_handle;
static uint16_t s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
static uint8_t s_own_addr_type;
static bool s_authorized;
static bool s_subscribed;
static bool s_pairing_open;
static int64_t s_pairing_deadline_us;
static QueueHandle_t s_rx_queue;
static QueueHandle_t s_diag_queue;
static char s_reassembly[BEATBOX_BLE_LINE_MAX];
static size_t s_reassembly_len;
static uint8_t s_expected_sequence;
static bool s_reassembly_active;
static uint8_t s_challenge[BEATBOX_BLE_CHALLENGE_BYTES];
static bool s_challenge_valid;

static int gap_event(struct ble_gap_event *event, void *argument);
static int start_advertising(void);

static void enqueue_diag(const char *format, ...)
{
    if (s_diag_queue == NULL || format == NULL) {
        return;
    }
    beatbox_ble_diag_line_t item = {0};
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(item.line, sizeof(item.line), format, args);
    va_end(args);
    if (length <= 0 || (size_t)length >= sizeof(item.line)) {
        return;
    }
    (void)xQueueSend(s_diag_queue, &item, 0);
}

static int configure_stable_beatbox_identity(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(BEATBOX_BLE_TRUST_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "open Beatbox BLE identity store: %s", esp_err_to_name(err));
        return BLE_HS_ESTORE_FAIL;
    }

    ble_addr_t address = {.type = BLE_ADDR_RANDOM};
    size_t address_size = sizeof(address.val);
    err = nvs_get_blob(handle, BEATBOX_BLE_IDENTITY_KEY, address.val, &address_size);
    const bool stored_static_address =
        err == ESP_OK && address_size == sizeof(address.val) &&
        (address.val[5] & 0xc0) == 0xc0;
    if (!stored_static_address) {
        const int generate_rc = ble_hs_id_gen_rnd(0, &address);
        if (generate_rc != 0) {
            nvs_close(handle);
            ESP_LOGE(TAG, "generate Beatbox BLE identity, rc=%d", generate_rc);
            return generate_rc;
        }
        err = nvs_set_blob(handle, BEATBOX_BLE_IDENTITY_KEY, address.val,
                           sizeof(address.val));
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
        if (err != ESP_OK) {
            nvs_close(handle);
            ESP_LOGE(TAG, "save Beatbox BLE identity: %s", esp_err_to_name(err));
            return BLE_HS_ESTORE_FAIL;
        }
    }
    nvs_close(handle);

    const int rc = ble_hs_id_set_rnd(address.val);
    if (rc == 0) {
        ESP_LOGI(TAG, "Beatbox BLE identity %02x:%02x:%02x:%02x:%02x:%02x",
                 address.val[5], address.val[4], address.val[3], address.val[2],
                 address.val[1], address.val[0]);
    }
    return rc;
}

static void close_pairing_window(void)
{
    s_pairing_open = false;
    s_pairing_deadline_us = 0;
}

static const char *client_slot_key(int slot)
{
    static const char *const keys[BEATBOX_BLE_TRUST_LIMIT] = {"client0", "client1", "client2"};
    return slot >= 0 && slot < BEATBOX_BLE_TRUST_LIMIT ? keys[slot] : "";
}

static esp_err_t client_read_slot(nvs_handle_t handle, int slot, beatbox_ble_client_t *client)
{
    size_t size = sizeof(*client);
    const esp_err_t err = nvs_get_blob(handle, client_slot_key(slot), client, &size);
    return err == ESP_OK && size == sizeof(*client) ? ESP_OK : err;
}

static bool constant_time_equal(const uint8_t *left, const uint8_t *right, size_t length)
{
    uint8_t difference = 0;
    for (size_t i = 0; i < length; ++i) {
        difference |= left[i] ^ right[i];
    }
    return difference == 0;
}

static int hex_nibble(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static bool decode_hex_exact(const char *hex, uint8_t *out, size_t out_len)
{
    if (hex == NULL || strlen(hex) != out_len * 2) {
        return false;
    }
    for (size_t i = 0; i < out_len; ++i) {
        const int high = hex_nibble(hex[i * 2]);
        const int low = hex_nibble(hex[i * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        out[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static void encode_hex(const uint8_t *bytes, size_t length, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < length; ++i) {
        out[i * 2] = digits[bytes[i] >> 4];
        out[i * 2 + 1] = digits[bytes[i] & 0x0f];
    }
    out[length * 2] = '\0';
}

static bool extract_quoted_field(const char *line, const char *key, char *out, size_t out_len)
{
    char pattern[32];
    const int pattern_len = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    if (pattern_len <= 0 || (size_t)pattern_len >= sizeof(pattern)) {
        return false;
    }
    const char *field = strstr(line, pattern);
    const char *colon = field == NULL ? NULL : strchr(field + pattern_len, ':');
    const char *first_quote = colon == NULL ? NULL : strchr(colon, '"');
    const char *last_quote = first_quote == NULL ? NULL : strchr(first_quote + 1, '"');
    if (last_quote == NULL || (size_t)(last_quote - first_quote) > out_len) {
        return false;
    }
    const size_t length = (size_t)(last_quote - first_quote - 1);
    memcpy(out, first_quote + 1, length);
    out[length] = '\0';
    return true;
}

static bool client_find(const uint8_t id[BEATBOX_BLE_CLIENT_ID_BYTES],
                        beatbox_ble_client_t *client)
{
    nvs_handle_t handle;
    if (nvs_open(BEATBOX_BLE_TRUST_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    bool found = false;
    for (int slot = 0; slot < BEATBOX_BLE_TRUST_LIMIT; ++slot) {
        beatbox_ble_client_t candidate = {0};
        if (client_read_slot(handle, slot, &candidate) == ESP_OK &&
            constant_time_equal(candidate.id, id, sizeof(candidate.id))) {
            if (client != NULL) {
                *client = candidate;
            }
            found = true;
            break;
        }
    }
    nvs_close(handle);
    return found;
}

static int client_count(void)
{
    nvs_handle_t handle;
    if (nvs_open(BEATBOX_BLE_TRUST_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return 0;
    }
    int count = 0;
    for (int slot = 0; slot < BEATBOX_BLE_TRUST_LIMIT; ++slot) {
        beatbox_ble_client_t candidate = {0};
        if (client_read_slot(handle, slot, &candidate) == ESP_OK) {
            ++count;
        }
    }
    nvs_close(handle);
    return count;
}

static esp_err_t client_store(const beatbox_ble_client_t *client)
{
    nvs_handle_t handle;
    ESP_RETURN_ON_ERROR(nvs_open(BEATBOX_BLE_TRUST_NAMESPACE, NVS_READWRITE, &handle), TAG,
                        "open Beatbox browser credential store");
    int target_slot = -1;
    for (int slot = 0; slot < BEATBOX_BLE_TRUST_LIMIT; ++slot) {
        beatbox_ble_client_t candidate = {0};
        const esp_err_t err = client_read_slot(handle, slot, &candidate);
        if (err == ESP_OK && constant_time_equal(candidate.id, client->id, sizeof(client->id))) {
            target_slot = slot;
            break;
        }
        if (err == ESP_ERR_NVS_NOT_FOUND && target_slot < 0) {
            target_slot = slot;
        }
    }
    uint8_t next_slot = 0;
    if (target_slot < 0) {
        if (nvs_get_u8(handle, BEATBOX_BLE_CLIENT_NEXT_KEY, &next_slot) != ESP_OK ||
            next_slot >= BEATBOX_BLE_TRUST_LIMIT) {
            next_slot = 0;
        }
        target_slot = next_slot;
    }
    next_slot = (uint8_t)((target_slot + 1) % BEATBOX_BLE_TRUST_LIMIT);
    esp_err_t err = nvs_set_blob(handle, client_slot_key(target_slot), client, sizeof(*client));
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, BEATBOX_BLE_CLIENT_NEXT_KEY, next_slot);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

static bool notify_line_unchecked(const char *line)
{
    if (line == NULL || !s_subscribed || s_connection_handle == BLE_HS_CONN_HANDLE_NONE) {
        return false;
    }
    const size_t line_length = strnlen(line, BEATBOX_BLE_LINE_MAX);
    if (line_length == 0 || line_length >= BEATBOX_BLE_LINE_MAX) {
        return false;
    }
    const uint16_t mtu = ble_att_mtu(s_connection_handle);
    size_t payload_limit = mtu > 5 ? (size_t)mtu - 5 : 18;
    if (payload_limit > 120) {
        payload_limit = 120;
    }

    uint8_t sequence = 0;
    size_t offset = 0;
    while (offset < line_length) {
        size_t payload_length = line_length - offset;
        if (payload_length > payload_limit) {
            payload_length = payload_limit;
        }
        uint8_t packet[122];
        packet[0] = (offset == 0 ? BEATBOX_BLE_FRAGMENT_START : 0) |
                    (offset + payload_length == line_length ? BEATBOX_BLE_FRAGMENT_END : 0);
        packet[1] = sequence++;
        memcpy(packet + BEATBOX_BLE_FRAGMENT_HEADER, line + offset, payload_length);
        struct os_mbuf *buffer = ble_hs_mbuf_from_flat(
            packet, payload_length + BEATBOX_BLE_FRAGMENT_HEADER);
        if (buffer == NULL ||
            ble_gatts_notify_custom(s_connection_handle, s_tx_value_handle, buffer) != 0) {
            return false;
        }
        offset += payload_length;
    }
    return true;
}

static void send_auth_error(const char *code)
{
    char line[128];
    snprintf(line, sizeof(line),
             "{\"t\":\"ble_auth_error\",\"code\":\"%s\",\"pairing\":%d}",
             code == NULL ? "unknown" : code, s_pairing_open ? 1 : 0);
    (void)notify_line_unchecked(line);
    enqueue_diag(
        "{\"t\":\"ble_diag\",\"event\":\"app_auth_failure\",\"code\":\"%s\",\"pairing_open\":%d}",
        code == NULL ? "unknown" : code, s_pairing_open ? 1 : 0);
}

static void authorize_browser(const char *mode)
{
    s_authorized = true;
    s_challenge_valid = false;
    char line[112];
    snprintf(line, sizeof(line),
             "{\"t\":\"ble_auth_ok\",\"mode\":\"%s\",\"v\":1}", mode);
    (void)notify_line_unchecked(line);
    enqueue_diag(
        "{\"t\":\"ble_diag\",\"event\":\"app_auth_success\",\"mode\":\"%s\",\"client_count\":%d}",
        mode, client_count());
}

static void send_auth_challenge(void)
{
    esp_fill_random(s_challenge, sizeof(s_challenge));
    s_challenge_valid = true;
    char nonce[BEATBOX_BLE_CHALLENGE_BYTES * 2 + 1];
    char line[160];
    encode_hex(s_challenge, sizeof(s_challenge), nonce);
    snprintf(line, sizeof(line),
             "{\"t\":\"ble_auth_challenge\",\"nonce\":\"%s\",\"pairing\":%d,\"v\":1}",
             nonce, s_pairing_open ? 1 : 0);
    const bool sent = notify_line_unchecked(line);
    enqueue_diag(
        "{\"t\":\"ble_diag\",\"event\":\"app_auth_challenge\",\"sent\":%d,\"pairing_open\":%d,\"client_count\":%d}",
        sent ? 1 : 0, s_pairing_open ? 1 : 0, client_count());
}

static int handle_pre_auth_line(const char *line)
{
    char type[32];
    if (!extract_quoted_field(line, "t", type, sizeof(type))) {
        send_auth_error("bad_message");
        return 0;
    }

    if (strcmp(type, "ble_enroll") == 0) {
        if (!s_pairing_open) {
            send_auth_error("pairing_closed");
            return 0;
        }
        char id_hex[BEATBOX_BLE_CLIENT_ID_BYTES * 2 + 1];
        char key_hex[BEATBOX_BLE_CLIENT_KEY_BYTES * 2 + 1];
        beatbox_ble_client_t client = {0};
        if (!extract_quoted_field(line, "id", id_hex, sizeof(id_hex)) ||
            !extract_quoted_field(line, "key", key_hex, sizeof(key_hex)) ||
            !decode_hex_exact(id_hex, client.id, sizeof(client.id)) ||
            !decode_hex_exact(key_hex, client.key, sizeof(client.key))) {
            send_auth_error("bad_enrollment");
            return 0;
        }
        const esp_err_t err = client_store(&client);
        if (err != ESP_OK) {
            send_auth_error("store_failed");
            return 0;
        }
        close_pairing_window();
        authorize_browser("enrolled");
        return 0;
    }

    if (strcmp(type, "ble_auth") == 0) {
        if (!s_challenge_valid) {
            send_auth_error("challenge_missing");
            return 0;
        }
        char id_hex[BEATBOX_BLE_CLIENT_ID_BYTES * 2 + 1];
        char proof_hex[BEATBOX_BLE_PROOF_BYTES * 2 + 1];
        uint8_t id[BEATBOX_BLE_CLIENT_ID_BYTES];
        uint8_t proof[BEATBOX_BLE_PROOF_BYTES];
        beatbox_ble_client_t client = {0};
        if (!extract_quoted_field(line, "id", id_hex, sizeof(id_hex)) ||
            !extract_quoted_field(line, "proof", proof_hex, sizeof(proof_hex)) ||
            !decode_hex_exact(id_hex, id, sizeof(id)) ||
            !decode_hex_exact(proof_hex, proof, sizeof(proof))) {
            send_auth_error("bad_proof");
            return 0;
        }
        if (!client_find(id, &client)) {
            send_auth_error("unknown_client");
            return 0;
        }
        const mbedtls_md_info_t *sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        uint8_t expected[BEATBOX_BLE_PROOF_BYTES];
        if (sha256 == NULL ||
            mbedtls_md_hmac(sha256, client.key, sizeof(client.key), s_challenge,
                            sizeof(s_challenge), expected) != 0 ||
            !constant_time_equal(expected, proof, sizeof(expected))) {
            send_auth_error("proof_mismatch");
            return 0;
        }
        authorize_browser("known");
        return 0;
    }

    send_auth_error("authorization_required");
    return 0;
}

static bool enqueue_complete_line(void)
{
    if (s_rx_queue == NULL || s_reassembly_len == 0 ||
        s_reassembly_len >= sizeof(s_reassembly)) {
        return false;
    }
    beatbox_ble_line_t item = {0};
    memcpy(item.line, s_reassembly, s_reassembly_len);
    item.line[s_reassembly_len] = '\0';
    return xQueueSend(s_rx_queue, &item, 0) == pdTRUE;
}

static int receive_fragment(const uint8_t *bytes, size_t length)
{
    if (length < BEATBOX_BLE_FRAGMENT_HEADER) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    const uint8_t flags = bytes[0];
    const uint8_t sequence = bytes[1];
    const uint8_t *payload = bytes + BEATBOX_BLE_FRAGMENT_HEADER;
    const size_t payload_length = length - BEATBOX_BLE_FRAGMENT_HEADER;

    if ((flags & BEATBOX_BLE_FRAGMENT_START) != 0) {
        s_reassembly_len = 0;
        s_reassembly_active = true;
        s_expected_sequence = sequence;
    }
    if (!s_reassembly_active || sequence != s_expected_sequence) {
        s_reassembly_active = false;
        s_reassembly_len = 0;
        return BLE_ATT_ERR_UNLIKELY;
    }
    s_expected_sequence++;
    if (payload_length == 0 || s_reassembly_len + payload_length >= sizeof(s_reassembly)) {
        s_reassembly_active = false;
        s_reassembly_len = 0;
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    memcpy(s_reassembly + s_reassembly_len, payload, payload_length);
    s_reassembly_len += payload_length;

    if ((flags & BEATBOX_BLE_FRAGMENT_END) != 0) {
        s_reassembly[s_reassembly_len] = '\0';
        const bool accepted = s_authorized ? enqueue_complete_line()
                                           : handle_pre_auth_line(s_reassembly) == 0;
        s_reassembly_active = false;
        s_reassembly_len = 0;
        if (!accepted) {
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        }
    }
    return 0;
}

static int characteristic_access(uint16_t connection_handle, uint16_t attribute_handle,
                                 struct ble_gatt_access_ctxt *context, void *argument)
{
    (void)attribute_handle;
    (void)argument;
    if (context->op != BLE_GATT_ACCESS_OP_WRITE_CHR ||
        connection_handle != s_connection_handle) {
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    uint8_t packet[256];
    const uint16_t total = OS_MBUF_PKTLEN(context->om);
    if (total > sizeof(packet)) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    uint16_t copied = 0;
    if (ble_hs_mbuf_to_flat(context->om, packet, total, &copied) != 0) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    return receive_fragment(packet, copied);
}

static const struct ble_gatt_chr_def s_characteristics[] = {
    {
        .uuid = &s_rx_uuid.u,
        .access_cb = characteristic_access,
        /* Windows Web Bluetooth exposes no PairAsync equivalent.  Keep ATT
         * plaintext and authorize the browser with a physical-window-issued
         * credential plus a fresh HMAC challenge on every connection. */
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = &s_tx_uuid.u,
        .access_cb = characteristic_access,
        /* The first notifications carry only the random authorization
         * challenge.  Beatbox application data remains withheld until the
         * browser proves possession of its stored credential. */
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_tx_value_handle,
    },
    {0},
};

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = s_characteristics,
    },
    {0},
};

static int start_advertising(void)
{
    struct ble_hs_adv_fields fields = {
        .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .name = (uint8_t *)BEATBOX_BLE_DEVICE_NAME,
        .name_len = sizeof(BEATBOX_BLE_DEVICE_NAME) - 1,
        .name_is_complete = 1,
    };
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        return rc;
    }
    struct ble_hs_adv_fields response = {
        .uuids128 = (ble_uuid128_t *)&s_service_uuid,
        .num_uuids128 = 1,
        .uuids128_is_complete = 1,
    };
    rc = ble_gap_adv_rsp_set_fields(&response);
    if (rc != 0) {
        return rc;
    }
    const struct ble_gap_adv_params parameters = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };
    return ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &parameters, gap_event, NULL);
}

static int gap_event(struct ble_gap_event *event, void *argument)
{
    (void)argument;
    struct ble_gap_conn_desc description;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT: {
        ESP_LOGW(TAG, "diag connect status=%d pairing_open=%d", event->connect.status,
                 s_pairing_open ? 1 : 0);
        if (event->connect.status != 0) {
            enqueue_diag(
                "{\"t\":\"ble_diag\",\"event\":\"connect\",\"status\":%d,\"pairing_open\":%d}",
                event->connect.status, s_pairing_open ? 1 : 0);
            (void)start_advertising();
            return 0;
        }
        s_connection_handle = event->connect.conn_handle;
        s_authorized = false;
        s_subscribed = false;
        s_challenge_valid = false;
        const int find_rc = ble_gap_conn_find(s_connection_handle, &description);
        enqueue_diag(
            "{\"t\":\"ble_diag\",\"event\":\"connect\",\"status\":0,\"find_rc\":%d,\"pairing_open\":%d,\"client_count\":%d}",
            find_rc, s_pairing_open ? 1 : 0, client_count());
        if (find_rc != 0) {
            (void)ble_gap_terminate(s_connection_handle, BLE_ERR_REM_USER_CONN_TERM);
            return 0;
        }
        ESP_LOGW(TAG, "diag waiting for browser application authorization");
        enqueue_diag(
            "{\"t\":\"ble_diag\",\"event\":\"app_auth_wait\",\"pairing_open\":%d}",
            s_pairing_open ? 1 : 0);
        return 0;
    }

    case BLE_GAP_EVENT_ENC_CHANGE: {
        const int find_rc = ble_gap_conn_find(event->enc_change.conn_handle, &description);
        const bool encrypted = find_rc == 0 && description.sec_state.encrypted;
        const bool bonded = find_rc == 0 && description.sec_state.bonded;
        enqueue_diag(
            "{\"t\":\"ble_diag\",\"event\":\"enc_change\",\"status\":%d,\"find_rc\":%d,\"encrypted\":%d,\"bonded\":%d,\"pairing_open\":%d}",
            event->enc_change.status, find_rc, encrypted ? 1 : 0, bonded ? 1 : 0,
            s_pairing_open ? 1 : 0);
        /* Link-level encryption is optional for this Web Bluetooth transport.
         * Authorization is exclusively the application challenge above. */
        return 0;
    }

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tx_value_handle) {
            s_subscribed = event->subscribe.cur_notify != 0;
            ESP_LOGW(TAG,
                     "diag subscribe notify=%d authorized=%d pairing_open=%d",
                     s_subscribed ? 1 : 0, s_authorized ? 1 : 0,
                     s_pairing_open ? 1 : 0);
            if (s_subscribed && !s_authorized) {
                send_auth_challenge();
            } else if (!s_subscribed) {
                s_challenge_valid = false;
            }
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGW(TAG, "diag disconnect reason=%d", event->disconnect.reason);
        enqueue_diag(
            "{\"t\":\"ble_diag\",\"event\":\"disconnect\",\"reason\":%d,\"authorized\":%d,\"subscribed\":%d}",
            event->disconnect.reason, s_authorized ? 1 : 0, s_subscribed ? 1 : 0);
        s_connection_handle = BLE_HS_CONN_HANDLE_NONE;
        s_authorized = false;
        s_subscribed = false;
        s_reassembly_active = false;
        s_reassembly_len = 0;
        s_challenge_valid = false;
        (void)start_advertising();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        (void)start_advertising();
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        const int find_rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &description);
        ESP_LOGW(TAG,
                 "diag legacy repeat_pairing ignored pairing_open=%d find_rc=%d",
                 s_pairing_open ? 1 : 0, find_rc);
        enqueue_diag(
            "{\"t\":\"ble_diag\",\"event\":\"repeat_pairing\",\"pairing_open\":%d,\"find_rc\":%d,\"action\":\"legacy_ignore\"}",
            s_pairing_open ? 1 : 0, find_rc);
        return BLE_GAP_REPEAT_PAIRING_IGNORE;
    }

    default:
        return 0;
    }
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset, reason=%d", reason);
    enqueue_diag("{\"t\":\"ble_diag\",\"event\":\"nimble_reset\",\"reason\":%d}",
                 reason);
}

static void on_sync(void)
{
    int rc = configure_stable_beatbox_identity();
    if (rc == 0) {
        rc = ble_hs_util_ensure_addr(1);
    }
    if (rc == 0) {
        rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    }
    int bond_count = -1;
    const int bond_count_rc = ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &bond_count);
    const int adv_rc = rc == 0 ? start_advertising() : BLE_HS_EUNKNOWN;
    enqueue_diag(
        "{\"t\":\"ble_diag\",\"event\":\"sync\",\"identity_rc\":%d,\"addr_type\":%u,\"legacy_bond_count_rc\":%d,\"legacy_bond_count\":%d,\"client_count\":%d,\"adv_rc\":%d}",
        rc, (unsigned)s_own_addr_type, bond_count_rc, bond_count, client_count(), adv_rc);
    if (rc != 0 || adv_rc != 0) {
        ESP_LOGE(TAG, "could not start Beatbox BLE advertising, identity_rc=%d adv_rc=%d", rc,
                 adv_rc);
    }
}

static void nimble_host_task(void *argument)
{
    (void)argument;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t beatbox_ble_init(void)
{
    ESP_RETURN_ON_FALSE(s_rx_queue == NULL, ESP_ERR_INVALID_STATE, TAG, "already initialized");
    esp_err_t err = nvs_flash_init();
    ESP_RETURN_ON_ERROR(err, TAG, "initialize shared NVS without erase");

    s_rx_queue = xQueueCreate(BEATBOX_BLE_QUEUE_DEPTH, sizeof(beatbox_ble_line_t));
    ESP_RETURN_ON_FALSE(s_rx_queue != NULL, ESP_ERR_NO_MEM, TAG, "create BLE RX queue");
    s_diag_queue =
        xQueueCreate(BEATBOX_BLE_DIAG_QUEUE_DEPTH, sizeof(beatbox_ble_diag_line_t));
    ESP_RETURN_ON_FALSE(s_diag_queue != NULL, ESP_ERR_NO_MEM, TAG,
                        "create BLE diagnostic queue");

    ESP_RETURN_ON_ERROR(nimble_port_init(), TAG, "initialize NimBLE");
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_bonding = 0;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_our_key_dist = 0;
    ble_hs_cfg.sm_their_key_dist = 0;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    int rc = ble_svc_gap_device_name_set(BEATBOX_BLE_DEVICE_NAME);
    if (rc == 0) {
        rc = ble_gatts_count_cfg(s_services);
    }
    if (rc == 0) {
        rc = ble_gatts_add_svcs(s_services);
    }
    ESP_RETURN_ON_FALSE(rc == 0, ESP_FAIL, TAG, "register Beatbox GATT service, rc=%d", rc);

    ble_store_config_init();
    nimble_port_freertos_init(nimble_host_task);
    ESP_LOGI(TAG, "Beatbox BLE browser authorization ready; new computers require stopped S7 hold");
    return ESP_OK;
}

esp_err_t beatbox_ble_open_pairing_window(int64_t now_us)
{
    ESP_RETURN_ON_FALSE(s_rx_queue != NULL, ESP_ERR_INVALID_STATE, TAG, "BLE not initialized");
    ESP_RETURN_ON_FALSE(!s_authorized, ESP_ERR_INVALID_STATE, TAG,
                        "disconnect current computer before adding another");
    s_pairing_open = true;
    s_pairing_deadline_us = now_us + BEATBOX_BLE_PAIRING_WINDOW_US;
    ESP_LOGI(TAG, "physical pairing window opened for 60 seconds");
    enqueue_diag(
        "{\"t\":\"ble_diag\",\"event\":\"pairing_window\",\"state\":\"open\",\"duration_ms\":60000}");
    if (s_subscribed && !s_authorized) {
        send_auth_challenge();
    }
    return ESP_OK;
}

void beatbox_ble_poll(int64_t now_us)
{
    if (s_pairing_open && now_us >= s_pairing_deadline_us) {
        close_pairing_window();
        ESP_LOGI(TAG, "physical pairing window timed out");
        enqueue_diag(
            "{\"t\":\"ble_diag\",\"event\":\"pairing_window\",\"state\":\"timeout\"}");
    }
}

bool beatbox_ble_pairing_open(void)
{
    return s_pairing_open;
}

bool beatbox_ble_authorized_connected(void)
{
    return s_authorized && s_connection_handle != BLE_HS_CONN_HANDLE_NONE;
}

bool beatbox_ble_pop_line(char *out, size_t out_len)
{
    if (out == NULL || out_len == 0 || s_rx_queue == NULL) {
        return false;
    }
    beatbox_ble_line_t item;
    if (xQueueReceive(s_rx_queue, &item, 0) != pdTRUE) {
        return false;
    }
    strlcpy(out, item.line, out_len);
    return true;
}

bool beatbox_ble_pop_diag_line(char *out, size_t out_len)
{
    if (out == NULL || out_len == 0 || s_diag_queue == NULL) {
        return false;
    }
    beatbox_ble_diag_line_t item;
    if (xQueueReceive(s_diag_queue, &item, 0) != pdTRUE) {
        return false;
    }
    strlcpy(out, item.line, out_len);
    return true;
}

void beatbox_ble_send_line(const char *line)
{
    if (!s_authorized) {
        return;
    }
    (void)notify_line_unchecked(line);
}
