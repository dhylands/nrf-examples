/**
 * Copyright (c) 2018, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/** @file
 *
 * @defgroup zigbee_examples_cli_agent_router main.c
 * @{
 * @ingroup zigbee_examples
 * @brief CLI agent for probing the Zigbee network.
 */
#include "zboss_api.h"
#include "zb_mem_config_max.h"
#include "zb_ha_configuration_tool.h"
#include "zb_error_handler.h"

#include "zigbee_cli.h"

#include "nrf_drv_usbd.h"
#include "nrf_drv_clock.h"
#include "boards.h"
#include "app_timer.h"
#include "app_usbd.h"
#include "app_usbd_cdc_acm.h"
#include "app_usbd_serial_num.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "dumpmem.h"
#include "slip.h"
#include "packet.h"

void user_usb_init(void);

// #define IEEE_CHANNEL_MASK           ZB_TRANSCEIVER_ALL_CHANNELS_MASK  /**< Allow all channels from 11-26 */
#define IEEE_CHANNEL_MASK           (1 << 15)
#define ERASE_PERSISTENT_CONFIG     ZB_TRUE                 /**< Do not erase NVRAM to save the network parameters after device reboot or power-off. NOTE: If this option is set to ZB_TRUE then do full device erase for all network devices before running other samples. */
#define ZIGBEE_NETWORK_STATE_LED    (BSP_BOARD_LED_2)       /**< LED indicating that light switch successfully joind ZigBee network. */
#define LED_CDC_ACM_TXRX            (BSP_BOARD_LED_3)

#if !defined ZB_ROUTER_ROLE
#error Define ZB_ROUTER_ROLE to compile CLI agent (Router) source code.
#endif

typedef struct cli_agent_ctx_s
{
  zb_uint8_t role;
} cli_agent_ctx_t;

static cli_agent_ctx_t    m_device_ctx;
static zb_uint8_t         m_attr_zcl_version   = ZB_ZCL_VERSION;
static zb_uint8_t         m_attr_power_source  = ZB_ZCL_BASIC_POWER_SOURCE_UNKNOWN;
static zb_uint16_t        m_attr_identify_time = 0;
static zb_bool_t          m_stack_started      = ZB_FALSE;

/* Declare attribute list for Basic cluster. */
ZB_ZCL_DECLARE_BASIC_ATTRIB_LIST(basic_attr_list, &m_attr_zcl_version, &m_attr_power_source);

/* Declare attribute list for Identify cluster. */
ZB_ZCL_DECLARE_IDENTIFY_ATTRIB_LIST(identify_attr_list, &m_attr_identify_time);

/* Declare cluster list for CLI Agent device. */
/* Only clusters Identify and Basic have attributes. */
ZB_HA_DECLARE_CONFIGURATION_TOOL_CLUSTER_LIST(cli_agent_clusters,
                                              basic_attr_list,
                                              identify_attr_list);

/* Declare endpoint for CLI Agent device. */
ZB_HA_DECLARE_CONFIGURATION_TOOL_EP(cli_agent_ep,
                                    ZIGBEE_CLI_ENDPOINT,
                                    cli_agent_clusters);

/* Declare application's device context (list of registered endpoints) for CLI Agent device. */
ZB_HA_DECLARE_CONFIGURATION_TOOL_CTX(cli_agent_ctx, cli_agent_ep);

#define CDC_ACM_COMM_INTERFACE  0
#define CDC_ACM_COMM_EPIN       NRF_DRV_USBD_EPIN2

#define CDC_ACM_DATA_INTERFACE  1
#define CDC_ACM_DATA_EPIN       NRF_DRV_USBD_EPIN1
#define CDC_ACM_DATA_EPOUT      NRF_DRV_USBD_EPOUT1

static void cdc_acm_user_ev_handler(app_usbd_class_inst_t const * p_inst,
                                    app_usbd_cdc_acm_user_event_t event);

APP_USBD_CDC_ACM_GLOBAL_DEF(m_app_cdc_acm,
                            cdc_acm_user_ev_handler,
                            CDC_ACM_COMM_INTERFACE,
                            CDC_ACM_DATA_INTERFACE,
                            CDC_ACM_COMM_EPIN,
                            CDC_ACM_DATA_EPIN,
                            CDC_ACM_DATA_EPOUT,
                            APP_USBD_CDC_COMM_PROTOCOL_AT_V250
);

#define READ_SIZE 64

static uint8_t m_rx_buffer[READ_SIZE];
// static uint8_t m_tx_buffer[NRF_DRV_USBD_EPSIZE];
// static bool m_send_flag = 0;
static SLIP_Parser_t  m_slipParser;

/**
 * @brief User event handler @ref app_usbd_cdc_acm_user_ev_handler_t (headphones)
 * */
static void cdc_acm_user_ev_handler(app_usbd_class_inst_t const * p_inst,
                                    app_usbd_cdc_acm_user_event_t event)
{
    // p_cdc_acm points to m_app_cdc_acm_0 or m_app_cdc_acm_1
    app_usbd_cdc_acm_t const * p_cdc_acm = app_usbd_cdc_acm_class_get(p_inst);

    switch (event)
    {
        case APP_USBD_CDC_ACM_USER_EVT_PORT_OPEN:
        {
#if defined(LED_CDC_ACM_OPEN)
            bsp_board_led_on(LED_CDC_ACM_OPEN);
#endif

            /*Setup first transfer*/
            memset(m_rx_buffer, 0xee, sizeof(m_rx_buffer));
            ret_code_t ret = app_usbd_cdc_acm_read_any(p_cdc_acm,
                                                       m_rx_buffer,
                                                       READ_SIZE);
            UNUSED_VARIABLE(ret);
            break;
        }
#if defined(LED_CDC_ACM_OPEN)
        case APP_USBD_CDC_ACM_USER_EVT_PORT_CLOSE:
            bsp_board_led_off(LED_CDC_ACM_OPEN);
            break;
#endif
        case APP_USBD_CDC_ACM_USER_EVT_TX_DONE:
            bsp_board_led_invert(LED_CDC_ACM_TXRX);
            break;
        case APP_USBD_CDC_ACM_USER_EVT_RX_DONE:
        {
            ret_code_t ret;
            do
            {
                /*Get amount of data transfered*/
                size_t bytesAvail = app_usbd_cdc_acm_rx_size(p_cdc_acm);
                SLIP_parseChunk(&m_slipParser, m_rx_buffer, bytesAvail);

                /* Fetch data until internal buffer is empty */
                ret = app_usbd_cdc_acm_read_any(p_cdc_acm,
                                                m_rx_buffer,
                                                READ_SIZE);
            } while (ret == NRF_SUCCESS);

            bsp_board_led_invert(LED_CDC_ACM_TXRX);
            break;
        }
        default:
            break;
    }
}

void WriteResponse(uint8_t *buf, size_t bufLen) {
  ret_code_t ret = app_usbd_cdc_acm_write(&m_app_cdc_acm, buf, bufLen);
  if (ret != NRF_SUCCESS)
  {
    NRF_LOG_ERROR("Failed to write %lu byte response", bufLen);
  }
}

static void log_init(void)
{
    ret_code_t err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();
}

/**@brief ZigBee stack event handler.
 *
 * @param[in]   param   Reference to ZigBee stack buffer used to pass arguments (signal).
 */
void zboss_signal_handler(zb_uint8_t param)
{
    zb_zdo_app_signal_hdr_t      * p_sg_p         = NULL;
    zb_zdo_signal_leave_params_t * p_leave_params = NULL;
    zb_zdo_app_signal_type_t       sig            = zb_get_app_signal(param, &p_sg_p);
    zb_ret_t                       status         = ZB_GET_APP_SIGNAL_STATUS(param);
    zb_nwk_device_type_t           role           = ZB_NWK_DEVICE_TYPE_NONE;

    switch (sig)
    {
        case ZB_BDB_SIGNAL_DEVICE_FIRST_START:
        case ZB_BDB_SIGNAL_DEVICE_REBOOT:
            if (status == RET_OK)
            {
                NRF_LOG_INFO("Device started OK. Start network steering. Reason: %d", sig);
                bsp_board_led_on(ZIGBEE_NETWORK_STATE_LED);
                UNUSED_RETURN_VALUE(bdb_start_top_level_commissioning(ZB_BDB_NETWORK_STEERING));
            }
            else
            {
                role = zb_get_network_role();
                NRF_LOG_ERROR("Device startup failed. Status: %d. Retry network formation after 1 second.", status);
                bsp_board_led_off(ZIGBEE_NETWORK_STATE_LED);
                zb_uint8_t mode = (ZB_BDB_NETWORK_STEERING) |
                                  ((role == ZB_NWK_DEVICE_TYPE_COORDINATOR) ? ZB_BDB_NETWORK_FORMATION : 0);
                UNUSED_RETURN_VALUE(
                    ZB_SCHEDULE_ALARM(
                        (zb_callback_t)bdb_start_top_level_commissioning,
                        mode,
                        ZB_TIME_ONE_SECOND
                    )
                );
            }
            break;

        case ZB_ZDO_SIGNAL_LEAVE:
            if (status == RET_OK)
            {
                bsp_board_led_off(ZIGBEE_NETWORK_STATE_LED);
                p_leave_params = ZB_ZDO_SIGNAL_GET_PARAMS(p_sg_p, zb_zdo_signal_leave_params_t);
                NRF_LOG_INFO("Network left. Leave type: %d", p_leave_params->leave_type);
            }
            else
            {
                NRF_LOG_ERROR("Unable to leave network. Status: %d", status);
            }
            break;

        case ZB_ZDO_SIGNAL_PRODUCTION_CONFIG_READY:
            if (status != RET_OK)
            {
                NRF_LOG_WARNING("Production config is not present or invalid");
            }
            break;

        case ZB_ZDO_SIGNAL_SKIP_STARTUP:
            NRF_LOG_INFO("Stack is started");
            m_stack_started = ZB_TRUE;
            break;

        case ZB_ZDO_SIGNAL_DEVICE_ANNCE:
            NRF_LOG_INFO("ZB_ZDO_SIGNAL_DEVICE_ANNCE, status = %d", status);
            break;

        case ZB_ZDO_SIGNAL_ERROR:
            NRF_LOG_INFO("ZB_ZDO_SIGNAL_ERROR, status = %d", status);
            break;

        case ZB_BDB_SIGNAL_STEERING:
            NRF_LOG_INFO("ZB_BDB_SIGNAL_STEERING, status = %d", status);
            break;

        case ZB_BDB_SIGNAL_FORMATION:
            NRF_LOG_INFO("ZB_BDB_SIGNAL_FORMATION, status = %d", status);
            break;

        case ZB_ZDO_SIGNAL_LEAVE_INDICATION:
            NRF_LOG_INFO("ZB_ZDO_SIGNAL_LEAVE_INDICATION, status = %d", status);
            break;

        default:
            /* Unhandled signal. For more information see: zb_zdo_app_signal_type_e and zb_ret_e */
            NRF_LOG_INFO("Unhandled signal %d. Status: %d", sig, status);
    }

    if (param)
    {
        ZB_FREE_BUF_BY_REF(param);
    }
}

// The zb_cli_init function initializes the initial log level to ERROR
// and doesn't provide any convenient way to change this. This changes
// the default log level for the app to be INFO instead.
static void fix_logging_level(void) {
    uint32_t num_modules = nrf_log_module_cnt_get();
    uint32_t backend_count = 0;
#if NRF_LOG_BACKEND_RTT_ENABLED
    backend_count++;
#endif
#if APP_USBD_ENABLED
    backend_count++;
#endif
#if defined(TX_PIN_NUMBER) && defined(RX_PIN_NUMBER)
    backend_count++;
#endif

    for (uint32_t module_id = 0; module_id < num_modules; module_id++) {
        const char *module_name = nrf_log_module_name_get(module_id, true);
        if (strcmp(module_name, "app") == 0) {
            for (uint32_t backend_id = 0; backend_id < backend_count; backend_id++) {
                nrf_log_module_filter_set(backend_id, module_id, NRF_LOG_SEVERITY_INFO);
            }
            break;
        }
    }
}

void user_usb_init(void) {
  NRF_LOG_INFO("user_usb_init");
  app_usbd_class_inst_t const * class_cdc_acm = app_usbd_cdc_acm_class_inst_get(&m_app_cdc_acm);
  ret_code_t ret = app_usbd_class_append(class_cdc_acm);
  APP_ERROR_CHECK(ret);
}

/**@brief Function for application main entry.
 */
int main(void)
{
    ret_code_t      ret;
    zb_ieee_addr_t  ieee_addr;

    UNUSED_VARIABLE(m_device_ctx);

    /* Intiialise the leds */
    bsp_board_init(BSP_INIT_LEDS);
    bsp_board_leds_off();

    /* Initialize loging system and GPIOs. */
    log_init();

    SLIP_initParser(&m_slipParser, PacketReceived);

#if defined(APP_USBD_ENABLED) && APP_USBD_ENABLED
    ret = nrf_drv_clock_init();
    APP_ERROR_CHECK(ret);
    nrf_drv_clock_lfclk_request(NULL);
#endif

    ret = app_timer_init();
    APP_ERROR_CHECK(ret);

    app_usbd_serial_num_generate();

    // Initialize the Zigbee CLI subsystem. This also has a side effect
    // of initializing the USB subsystem if APP_USBD_ENABLED is defined.
    zb_cli_init(ZIGBEE_CLI_ENDPOINT);

    fix_logging_level();

    NRF_LOG_INFO("cli_agent_router started");
    NRF_LOG_PROCESS();

    /* Set ZigBee stack logging level and traffic dump subsystem. */
    ZB_SET_TRACE_LEVEL(ZIGBEE_TRACE_LEVEL);
    ZB_SET_TRACE_MASK(ZIGBEE_TRACE_MASK);
    ZB_SET_TRAF_DUMP_OFF();

    /* Initialize ZigBee stack. */
    ZB_INIT("cli_agent_router");

    /* Set device address to the value read from FICR registers. */
    zb_osif_get_ieee_eui64(ieee_addr);
    zb_set_long_address(ieee_addr);

    zb_set_bdb_primary_channel_set(IEEE_CHANNEL_MASK);

    /* Register CLI Agent device context (endpoints). */
    ZB_AF_REGISTER_DEVICE_CTX(&cli_agent_ctx);

    /* Set the endpoint receive hook */
    ZB_AF_SET_ENDPOINT_HANDLER(ZIGBEE_CLI_ENDPOINT, cli_agent_ep_handler);

#if 1
    zb_ext_pan_id_t extPanId;
    zb_ext_pan_id_t zeroPanId;

    memset(zeroPanId, 0, sizeof(zeroPanId));
    zb_get_extended_pan_id(extPanId);
    if (memcmp(extPanId, zeroPanId, sizeof(zeroPanId)) == 0) {
      // Extended PAN ID hasn't been set yet. Use the MAC address
      memcpy(extPanId, ieee_addr, sizeof(extPanId));
      zb_set_extended_pan_id(extPanId);
      NRF_LOG_INFO("Setting extended PAN Id to Mac address");
      NRF_LOG_HEXDUMP_INFO(extPanId, sizeof(extPanId));
      NRF_LOG_PROCESS();
    }
#endif

    uint32_t channelMask = zb_get_bdb_primary_channel_set();
    NRF_LOG_INFO("channelMask = 0x%08x", channelMask);
    NRF_LOG_PROCESS();
    zb_set_network_coordinator_role(channelMask);

    zb_set_nvram_erase_at_start(ERASE_PERSISTENT_CONFIG);

    NRF_LOG_INFO("About to call zboss_start");
    NRF_LOG_PROCESS();
    zb_ret_t zb_err_code = zboss_start();
    ZB_ERROR_CHECK(zb_err_code);

    NRF_LOG_INFO("About to enter main loop");
    NRF_LOG_PROCESS();

    /* Start ZigBee stack. */
    while(1)
    {
        //if (m_stack_started)
        {
            zboss_main_loop_iteration();
        }
        UNUSED_RETURN_VALUE(NRF_LOG_PROCESS());
        UNUSED_RETURN_VALUE(zb_cli_process());
    }
}


/**
 * @}
 */
