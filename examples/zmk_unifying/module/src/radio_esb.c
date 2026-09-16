/*!
 * \file radio_esb.c
 * \brief Nordic ESB PTX 适配，对接 unifying_interface。
 */

#include <zmk_unifying/radio_esb.h>

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <esb.h>

#include "unifying_utils.h"

LOG_MODULE_REGISTER(radio_esb, CONFIG_LOG_DEFAULT_LEVEL);

#ifndef CONFIG_ESB_MAX_PAYLOAD_LENGTH
#define CONFIG_ESB_MAX_PAYLOAD_LENGTH 32
#endif

#define RADIO_RX_QUEUE_DEPTH 4
#define RADIO_TX_TIMEOUT_MS 150

struct radio_rx_slot {
	uint8_t length;
	uint8_t data[UNIFYING_MAX_PAYLOAD_LEN];
};

static struct k_sem tx_done_sem;
static struct k_mutex radio_mutex;

static volatile bool tx_ok;
static atomic_t sleeping;
static bool esb_ready;

static uint8_t last_address_msb[UNIFYING_ADDRESS_LEN];
static uint8_t last_channel;
static bool have_address;

static struct radio_rx_slot rx_queue[RADIO_RX_QUEUE_DEPTH];
static uint8_t rx_head;
static uint8_t rx_tail;
static uint8_t rx_count;
static uint8_t last_rx_size;

static void rx_queue_clear(void)
{
	rx_head = 0;
	rx_tail = 0;
	rx_count = 0;
	last_rx_size = 0;
}

static void rx_queue_push(const uint8_t *data, uint8_t length)
{
	if (length == 0 || length > UNIFYING_MAX_PAYLOAD_LEN) {
		return;
	}

	if (rx_count >= RADIO_RX_QUEUE_DEPTH) {
		/* 丢弃最旧的 ACK，为新数据腾出空间 */
		rx_head = (rx_head + 1) % RADIO_RX_QUEUE_DEPTH;
		rx_count--;
	}

	struct radio_rx_slot *slot = &rx_queue[rx_tail];

	slot->length = length;
	memcpy(slot->data, data, length);
	rx_tail = (rx_tail + 1) % RADIO_RX_QUEUE_DEPTH;
	rx_count++;
	last_rx_size = length;
}

static void drain_esb_rx(void)
{
	struct esb_payload rx;

	while (esb_read_rx_payload(&rx) == 0) {
		rx_queue_push(rx.data, rx.length);
	}
}

static void radio_esb_event_handler(const struct esb_evt *event)
{
	switch (event->evt_id) {
	case ESB_EVENT_TX_SUCCESS:
		tx_ok = true;
		drain_esb_rx();
		k_sem_give(&tx_done_sem);
		break;
	case ESB_EVENT_TX_FAILED:
		tx_ok = false;
		esb_flush_tx();
		k_sem_give(&tx_done_sem);
		break;
	case ESB_EVENT_RX_RECEIVED:
		drain_esb_rx();
		break;
	default:
		break;
	}
}

static int apply_address_msb(const uint8_t address_msb[UNIFYING_ADDRESS_LEN])
{
	uint8_t address_lsb[UNIFYING_ADDRESS_LEN];
	uint8_t base_addr[4];
	uint8_t prefix[1];
	int err;

	/* 库内地址为 MSB first；nRF/ESB 使用 LSB first。 */
	unifying_copy_reverse(address_lsb, address_msb, UNIFYING_ADDRESS_LEN);

	/* pipe0 地址 = prefix(1) + base(4)，prefix 为 on-air 最低字节。 */
	prefix[0] = address_lsb[0];
	memcpy(base_addr, &address_lsb[1], sizeof(base_addr));

	err = esb_set_base_address_0(base_addr);
	if (err) {
		return err;
	}

	err = esb_set_prefixes(prefix, 1);
	if (err) {
		return err;
	}

	err = esb_enable_pipes(BIT(0));
	if (err) {
		return err;
	}

	memcpy(last_address_msb, address_msb, UNIFYING_ADDRESS_LEN);
	have_address = true;
	return 0;
}

static int esb_configure(void)
{
	struct esb_config config = ESB_DEFAULT_CONFIG;
	int err;

	config.protocol = ESB_PROTOCOL_ESB_DPL;
	config.mode = ESB_MODE_PTX;
	config.event_handler = radio_esb_event_handler;
	config.bitrate = ESB_BITRATE_2MBPS;
	config.crc = ESB_CRC_16BIT;
	config.tx_output_power = 4; /* ~+4 dBm，接近 RF24 PA_MAX */
	config.retransmit_delay = 3750; /* 对齐 RF24 setRetries(15, *) 的 15*250us */
	config.retransmit_count = 15;   /* 提高抗干扰，减轻偶发 TRANSMIT_ERROR */
	config.tx_mode = ESB_TXMODE_AUTO;
	config.payload_length = UNIFYING_MAX_PAYLOAD_LEN;
	config.selective_auto_ack = false;
	config.use_fast_ramp_up = false;

	err = esb_init(&config);
	if (err) {
		LOG_ERR("esb_init failed: %d", err);
		return err;
	}

	err = esb_set_address_length(UNIFYING_ADDRESS_LEN);
	if (err) {
		LOG_ERR("esb_set_address_length failed: %d", err);
		return err;
	}

	esb_ready = true;
	atomic_set(&sleeping, 0);
	return 0;
}

static uint8_t radio_transmit_payload(const uint8_t *payload, uint8_t length)
{
	struct esb_payload tx = {0};
	int err;

	if (atomic_get(&sleeping) || !esb_ready) {
		return 1;
	}

	if (payload == NULL || length == 0 || length > CONFIG_ESB_MAX_PAYLOAD_LENGTH) {
		return 1;
	}

	k_mutex_lock(&radio_mutex, K_FOREVER);

	while (k_sem_take(&tx_done_sem, K_NO_WAIT) == 0) {
		/* 清空残留信号 */
	}

	tx.pipe = 0;
	tx.length = length;
	tx.noack = false;
	memcpy(tx.data, payload, length);

	err = esb_write_payload(&tx);
	if (err) {
		LOG_WRN("esb_write_payload failed: %d", err);
		k_mutex_unlock(&radio_mutex);
		return 1;
	}

	err = k_sem_take(&tx_done_sem, K_MSEC(RADIO_TX_TIMEOUT_MS));
	if (err) {
		LOG_WRN("TX timeout");
		esb_flush_tx();
		k_mutex_unlock(&radio_mutex);
		return 1;
	}

	k_mutex_unlock(&radio_mutex);
	return tx_ok ? 0 : 1;
}

static uint8_t radio_receive_payload(uint8_t *payload, uint8_t length)
{
	uint8_t copy_len;

	if (payload == NULL || length == 0 || rx_count == 0) {
		return 0;
	}

	k_mutex_lock(&radio_mutex, K_FOREVER);

	if (rx_count == 0) {
		k_mutex_unlock(&radio_mutex);
		return 0;
	}

	struct radio_rx_slot *slot = &rx_queue[rx_head];

	copy_len = slot->length < length ? slot->length : length;
	memcpy(payload, slot->data, copy_len);
	last_rx_size = slot->length;

	rx_head = (rx_head + 1) % RADIO_RX_QUEUE_DEPTH;
	rx_count--;

	k_mutex_unlock(&radio_mutex);
	return copy_len;
}

static bool radio_payload_available(void)
{
	return rx_count > 0;
}

static uint8_t radio_payload_size(void)
{
	if (rx_count == 0) {
		return 0;
	}

	return rx_queue[rx_head].length;
}

static uint8_t radio_set_address(const uint8_t address[UNIFYING_ADDRESS_LEN])
{
	int err;

	if (atomic_get(&sleeping) || !esb_ready) {
		memcpy(last_address_msb, address, UNIFYING_ADDRESS_LEN);
		have_address = true;
		return 1;
	}

	k_mutex_lock(&radio_mutex, K_FOREVER);
	err = apply_address_msb(address);
	k_mutex_unlock(&radio_mutex);

	if (err) {
		LOG_ERR("set_address failed: %d", err);
		return 1;
	}

	LOG_INF("RF address set");
	return 0;
}

static uint8_t radio_set_channel(uint8_t channel)
{
	int err;

	last_channel = channel;

	if (atomic_get(&sleeping) || !esb_ready) {
		return 1;
	}

	k_mutex_lock(&radio_mutex, K_FOREVER);
	err = esb_set_rf_channel(channel);
	k_mutex_unlock(&radio_mutex);

	if (err) {
		LOG_ERR("set_channel %u failed: %d", channel, err);
		return 1;
	}

	LOG_DBG("RF channel %u", channel);
	return 0;
}

static uint32_t radio_time(void)
{
	return k_uptime_get_32();
}

int radio_esb_init(struct unifying_interface *interface)
{
	int err;

	if (interface == NULL) {
		return -EINVAL;
	}

	k_sem_init(&tx_done_sem, 0, 1);
	k_mutex_init(&radio_mutex);
	rx_queue_clear();
	have_address = false;
	last_channel = unifying_channels[0];
	atomic_set(&sleeping, 0);
	esb_ready = false;

	err = esb_configure();
	if (err) {
		return err;
	}

	err = esb_set_rf_channel(last_channel);
	if (err) {
		LOG_ERR("initial channel failed: %d", err);
		return err;
	}

	err = unifying_interface_init(interface,
					 radio_transmit_payload,
					 radio_receive_payload,
					 radio_payload_available,
					 radio_payload_size,
					 radio_set_address,
					 radio_set_channel,
					 radio_time,
					 NULL);
	if (err != UNIFYING_SUCCESS) {
		return -EIO;
	}

	LOG_INF("ESB radio ready");
	return 0;
}

void radio_esb_sleep(void)
{
	k_mutex_lock(&radio_mutex, K_FOREVER);

	if (esb_ready) {
		esb_disable();
		esb_ready = false;
	}

	rx_queue_clear();
	atomic_set(&sleeping, 1);
	k_mutex_unlock(&radio_mutex);
	LOG_INF("RADIO sleeping");
}

int radio_esb_wake(void)
{
	int err;

	k_mutex_lock(&radio_mutex, K_FOREVER);

	rx_queue_clear();
	err = esb_configure();
	if (err) {
		k_mutex_unlock(&radio_mutex);
		return err;
	}

	err = esb_set_rf_channel(last_channel);
	if (err) {
		k_mutex_unlock(&radio_mutex);
		return err;
	}

	if (have_address) {
		err = apply_address_msb(last_address_msb);
		if (err) {
			k_mutex_unlock(&radio_mutex);
			return err;
		}
	}

	atomic_set(&sleeping, 0);
	k_mutex_unlock(&radio_mutex);
	LOG_INF("RADIO awake, channel %u", last_channel);
	return 0;
}

bool radio_esb_is_sleeping(void)
{
	return atomic_get(&sleeping) != 0;
}

uint8_t radio_esb_current_channel(void)
{
	return last_channel;
}

uint8_t radio_esb_last_channel(void)
{
	return last_channel;
}
