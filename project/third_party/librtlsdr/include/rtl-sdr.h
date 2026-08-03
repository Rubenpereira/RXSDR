/*
 * rtl-sdr.h - API pública da librtlsdr (compatível com rtlsdr.dll Windows)
 * Baseado no librtlsdr open-source (GPL v2+)
 * https://github.com/steve-m/librtlsdr
 *
 * Este arquivo é um header standalone para compilar contra a rtlsdr.dll
 * pré-compilada sem precisar do SDK completo.
 */
#pragma once
#ifndef __RTL_SDR_H
#define __RTL_SDR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct rtlsdr_dev rtlsdr_dev_t;

typedef void(*rtlsdr_read_async_cb_t)(unsigned char *buf, uint32_t len, void *ctx);

/* ------------------------------------------------------------------ */
/*  Enumeração e abertura                                              */
/* ------------------------------------------------------------------ */

uint32_t rtlsdr_get_device_count(void);

const char *rtlsdr_get_device_name(uint32_t index);

/*!
 * Get USB device strings.
 * \param index the device index
 * \param manufact manufacturer name, at least 256 bytes
 * \param product product name, at least 256 bytes
 * \param serial serial number, at least 256 bytes
 * \return 0 on success
 */
int rtlsdr_get_device_usb_strings(uint32_t index,
                                   char *manufact,
                                   char *product,
                                   char *serial);

/*!
 * Get device index by USB serial string descriptor.
 * \param serial serial string of the device
 * \return device index of first device where the name matched
 * \return -1 if name is NULL
 * \return -2 if no devices were found at all
 * \return -3 if devices were found, but none with matching name
 */
int rtlsdr_get_index_by_serial(const char *serial);

int rtlsdr_open(rtlsdr_dev_t **dev, uint32_t index);
int rtlsdr_close(rtlsdr_dev_t *dev);

/* ------------------------------------------------------------------ */
/*  Configuração                                                       */
/* ------------------------------------------------------------------ */

/* Set crystal oscillator frequencies (Hz) */
int rtlsdr_set_xtal_freq(rtlsdr_dev_t *dev, uint32_t rtl_freq, uint32_t tuner_freq);
int rtlsdr_get_xtal_freq(rtlsdr_dev_t *dev, uint32_t *rtl_freq, uint32_t *tuner_freq);

int rtlsdr_get_usb_strings(rtlsdr_dev_t *dev, char *manufact, char *product, char *serial);

int rtlsdr_write_eeprom(rtlsdr_dev_t *dev, uint8_t *data, uint8_t offset, uint16_t len);
int rtlsdr_read_eeprom(rtlsdr_dev_t *dev, uint8_t *data, uint8_t offset, uint16_t len);

int rtlsdr_set_center_freq(rtlsdr_dev_t *dev, uint32_t freq);
int rtlsdr_set_center_freq64(rtlsdr_dev_t *dev, uint64_t freq);

uint32_t rtlsdr_get_center_freq(rtlsdr_dev_t *dev);
uint64_t rtlsdr_get_center_freq64(rtlsdr_dev_t *dev);

int rtlsdr_set_freq_correction(rtlsdr_dev_t *dev, int ppm);
int rtlsdr_get_freq_correction(rtlsdr_dev_t *dev);

typedef enum rtlsdr_tuner {
    RTLSDR_TUNER_UNKNOWN = 0,
    RTLSDR_TUNER_E4000,
    RTLSDR_TUNER_FC0012,
    RTLSDR_TUNER_FC0013,
    RTLSDR_TUNER_FC2580,
    RTLSDR_TUNER_R820T,
    RTLSDR_TUNER_R828D
} rtlsdr_tuner_t;

rtlsdr_tuner_t rtlsdr_get_tuner_type(rtlsdr_dev_t *dev);

int rtlsdr_get_tuner_gains(rtlsdr_dev_t *dev, int *gains);
int rtlsdr_set_tuner_gain(rtlsdr_dev_t *dev, int gain);
int rtlsdr_set_tuner_bandwidth(rtlsdr_dev_t *dev, uint32_t bw);
int rtlsdr_get_tuner_gain(rtlsdr_dev_t *dev);

int rtlsdr_set_tuner_if_gain(rtlsdr_dev_t *dev, int stage, int gain);

int rtlsdr_set_tuner_gain_mode(rtlsdr_dev_t *dev, int manual);

int rtlsdr_set_sample_rate(rtlsdr_dev_t *dev, uint32_t rate);
uint32_t rtlsdr_get_sample_rate(rtlsdr_dev_t *dev);

int rtlsdr_set_testmode(rtlsdr_dev_t *dev, int on);

int rtlsdr_set_agc_mode(rtlsdr_dev_t *dev, int on);

int rtlsdr_set_direct_sampling(rtlsdr_dev_t *dev, int on);
int rtlsdr_get_direct_sampling(rtlsdr_dev_t *dev);

int rtlsdr_set_offset_tuning(rtlsdr_dev_t *dev, int on);
int rtlsdr_get_offset_tuning(rtlsdr_dev_t *dev);

/* Bias-T (alimentação de 4.5V no conector SMA) */
int rtlsdr_set_bias_tee(rtlsdr_dev_t *dev, int on);
int rtlsdr_set_bias_tee_gpio(rtlsdr_dev_t *dev, int gpio, int on);

/* ------------------------------------------------------------------ */
/*  Leitura de amostras                                                */
/* ------------------------------------------------------------------ */

int rtlsdr_reset_buffer(rtlsdr_dev_t *dev);

int rtlsdr_read_sync(rtlsdr_dev_t *dev, void *buf, int len, int *n_read);

/*!
 * Read samples from the device asynchronously. This function will block until
 * it is being canceled using rtlsdr_cancel_async()
 * \param dev the device handle given by rtlsdr_open()
 * \param cb callback function to return received samples
 * \param ctx user specific context to pass via the callback function
 * \param buf_num optional buffer count, buf_num * buf_len = overall buffer size,
 *                set to 0 for default buffer count (15)
 * \param buf_len optional buffer length, must be multiple of 512, set to 0 for
 *                default buffer length (16 * 32 * 512 = 262144 bytes)
 * \return 0 on success
 */
int rtlsdr_read_async(rtlsdr_dev_t *dev,
                       rtlsdr_read_async_cb_t cb,
                       void *ctx,
                       uint32_t buf_num,
                       uint32_t buf_len);

int rtlsdr_cancel_async(rtlsdr_dev_t *dev);

/* ------------------------------------------------------------------ */
/*  GPIO / IR / I2C / versão                                           */
/* ------------------------------------------------------------------ */

int rtlsdr_set_gpio_output(rtlsdr_dev_t *dev, uint8_t gpio);
int rtlsdr_set_gpio_bit(rtlsdr_dev_t *dev, uint8_t gpio, int val);
int rtlsdr_get_gpio_bit(rtlsdr_dev_t *dev, uint8_t gpio, int *val);
int rtlsdr_set_gpio_byte(rtlsdr_dev_t *dev, uint8_t byte);
int rtlsdr_get_gpio_byte(rtlsdr_dev_t *dev, int *byte);

int rtlsdr_ir_query(rtlsdr_dev_t *dev, uint8_t *buf, size_t buf_len);

int rtlsdr_set_tuner_i2c_register(rtlsdr_dev_t *dev, unsigned i2c_register,
                                   unsigned mask, unsigned data);
int rtlsdr_get_tuner_i2c_register(rtlsdr_dev_t *dev, unsigned char *data,
                                   int *len, int *strength);

const char *rtlsdr_get_version(void);
uint32_t    rtlsdr_get_ver_id(void);
int         rtlsdr_is_tuner_PLL_locked(rtlsdr_dev_t *dev);

int rtlsdr_get_opt_help(int longInfo);
int rtlsdr_set_opt_string(rtlsdr_dev_t *dev, const char *opts, int verbose);

#ifdef __cplusplus
}
#endif

#endif /* __RTL_SDR_H */
