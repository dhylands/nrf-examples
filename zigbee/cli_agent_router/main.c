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

#include "zigbee_cli.h"

#include "nrf_drv_clock.h"
#include "boards.h"
#include "bsp.h"
#include "app_timer.h"
#include "app_error.h"
#include "app_util.h"
#include "app_usbd_core.h"
#include "app_usbd.h"
#include "app_usbd_string_desc.h"
#include "app_usbd_cdc_acm.h"
#include "app_usbd_serial_num.h"


#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"


#define IEEE_CHANNEL_MASK                   (1l << ZIGBEE_CHANNEL)              /**< Scan only one, predefined channel to find the coordinator. */
#define ERASE_PERSISTENT_CONFIG             ZB_FALSE                            /**< Do not erase NVRAM to save the network parameters after device reboot or power-off. NOTE: If this option is set to ZB_TRUE then do full device erase for all network devices before running other samples. */

#define LED_USB_RESUME              (BSP_BOARD_LED_0)
#define LED_CDC_ACM_OPEN            (BSP_BOARD_LED_1)
#define ZIGBEE_NETWORK_STATE_LED    (BSP_BOARD_LED_2)   /**< LED indicating that light switch successfully joind ZigBee network. */

#define BTN_CDC_DATA_SEND       0
#define BTN_CDC_NOTIFY_SEND     1
#define BTN_CDC_DATA_KEY_RELEASE        (bsp_event_t)(BSP_EVENT_KEY_LAST + 1)

#if !defined ZB_ROUTER_ROLE
#error Define ZB_ROUTER_ROLE to compile CLI agent (Router) source code.
#endif

static void cdc_acm_user_ev_handler(app_usbd_class_inst_t const * p_inst,
                                    app_usbd_cdc_acm_user_event_t event);

#define CDC_ACM_COMM_INTERFACE  0
#define CDC_ACM_COMM_EPIN       NRF_DRV_USBD_EPIN2

#define CDC_ACM_DATA_INTERFACE  1
#define CDC_ACM_DATA_EPIN       NRF_DRV_USBD_EPIN1
#define CDC_ACM_DATA_EPOUT      NRF_DRV_USBD_EPOUT1

#define READ_SIZE 1

/**
 * @brief CDC_ACM class instance
 * */
APP_USBD_CDC_ACM_GLOBAL_DEF(m_app_cdc_acm,
                            cdc_acm_user_ev_handler,
                            CDC_ACM_COMM_INTERFACE,
                            CDC_ACM_DATA_INTERFACE,
                            CDC_ACM_COMM_EPIN,
                            CDC_ACM_DATA_EPIN,
                            CDC_ACM_DATA_EPOUT,
                            APP_USBD_CDC_COMM_PROTOCOL_AT_V250
);

typedef struct cli_agent_ctx_s
{
  zb_uint8_t role;
} cli_agent_ctx_t;

static cli_agent_ctx_t    m_device_ctx;
static zb_uint8_t         m_attr_zcl_version   = ZB_ZCL_VERSION;
static zb_uint8_t         m_attr_power_source  = ZB_ZCL_BASIC_POWER_SOURCE_UNKNOWN;
static zb_uint16_t        m_attr_identify_time = 0;
static zb_bool_t          m_stack_started      = ZB_FALSE;
static char               m_rx_buffer[READ_SIZE];
static char               m_tx_buffer[NRF_DRV_USBD_EPSIZE];
static bool               m_send_flag = 0;



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

/**
 * @brief User event handler @ref app_usbd_cdc_acm_user_ev_handler_t (headphones)
 * */
static void cdc_acm_user_ev_handler(app_usbd_class_inst_t const * p_inst,
                                    app_usbd_cdc_acm_user_event_t event)
{
    app_usbd_cdc_acm_t const * p_cdc_acm = app_usbd_cdc_acm_class_get(p_inst);

    switch (event)
    {
        case APP_USBD_CDC_ACM_USER_EVT_PORT_OPEN:
        {
            bsp_board_led_on(LED_CDC_ACM_OPEN);

            /*Setup first transfer*/
            ret_code_t ret = app_usbd_cdc_acm_read(p_cdc_acm,
                                                   m_rx_buffer,
                                                   READ_SIZE);
            UNUSED_VARIABLE(ret);
            break;
        }
        case APP_USBD_CDC_ACM_USER_EVT_PORT_CLOSE:
            bsp_board_led_off(LED_CDC_ACM_OPEN);
            break;
        case APP_USBD_CDC_ACM_USER_EVT_TX_DONE:
            //bsp_board_led_invert(LED_CDC_ACM_TX);
            break;
        case APP_USBD_CDC_ACM_USER_EVT_RX_DONE:
        {
            ret_code_t ret;
            NRF_LOG_INFO("Bytes waiting: %d",
                         app_usbd_cdc_acm_bytes_stored(p_cdc_acm));
            do
            {
                /*Get amount of data transfered*/
                size_t size = app_usbd_cdc_acm_rx_size(p_cdc_acm);
                NRF_LOG_INFO("RX: size: %lu char: %c",
                             size, m_rx_buffer[0]);

                /* Fetch data until internal buffer is empty */
                ret = app_usbd_cdc_acm_read(p_cdc_acm,
                                            m_rx_buffer,
                                            READ_SIZE);
            } while (ret == NRF_SUCCESS);

            //bsp_board_led_invert(LED_CDC_ACM_RX);
            break;
        }
        default:
            break;
    }
}

static void usbd_user_ev_handler(app_usbd_event_type_t event)
{
    switch (event)
    {
        case APP_USBD_EVT_DRV_SUSPEND:
            bsp_board_led_off(LED_USB_RESUME);
            break;
        case APP_USBD_EVT_DRV_RESUME:
            bsp_board_led_on(LED_USB_RESUME);
            break;
        case APP_USBD_EVT_STARTED:
            break;
        case APP_USBD_EVT_STOPPED:
            app_usbd_disable();
            bsp_board_leds_off();
            break;
        case APP_USBD_EVT_POWER_DETECTED:
            NRF_LOG_INFO("USB power detected");

            if (!nrf_drv_usbd_is_enabled())
            {
                app_usbd_enable();
            }
            break;
        case APP_USBD_EVT_POWER_REMOVED:
            NRF_LOG_INFO("USB power removed");
            app_usbd_stop();
            break;
        case APP_USBD_EVT_POWER_READY:
            NRF_LOG_INFO("USB ready");
            app_usbd_start();
            break;
        default:
            break;
    }
}

static void bsp_event_callback(bsp_event_t ev)
{
    ret_code_t ret;
    switch ((unsigned int)ev)
    {
        case CONCAT_2(BSP_EVENT_KEY_, BTN_CDC_DATA_SEND):
        {
            m_send_flag = 1;
            break;
        }

        case BTN_CDC_DATA_KEY_RELEASE :
        {
            m_send_flag = 0;
            break;
        }

        case CONCAT_2(BSP_EVENT_KEY_, BTN_CDC_NOTIFY_SEND):
        {
            ret = app_usbd_cdc_acm_serial_state_notify(&m_app_cdc_acm,
                                                       APP_USBD_CDC_ACM_SERIAL_STATE_BREAK,
                                                       false);
            UNUSED_VARIABLE(ret);
            break;
        }

        default:
            return; // no implementation needed
    }
}

static void init_bsp(void)
{
    ret_code_t ret;
    ret = bsp_init(BSP_INIT_BUTTONS, bsp_event_callback);
    APP_ERROR_CHECK(ret);

    UNUSED_RETURN_VALUE(bsp_event_to_button_action_assign(BTN_CDC_DATA_SEND,
                                                          BSP_BUTTON_ACTION_RELEASE,
                                                          BTN_CDC_DATA_KEY_RELEASE));

    /* Configure LEDs */
    bsp_board_init(BSP_INIT_LEDS);
    bsp_board_leds_off();
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

        default:
            /* Unhandled signal. For more information see: zb_zdo_app_signal_type_e and zb_ret_e */
            NRF_LOG_INFO("Unhandled signal %d. Status: %d", sig, status);
    }

    if (param)
    {
        ZB_FREE_BUF_BY_REF(param);
    }
}

/**@brief Function for application main entry.
 */
int main(void)
{
    ret_code_t     ret;
    zb_ieee_addr_t ieee_addr;
    static const app_usbd_config_t usbd_config = {
        .ev_state_proc = usbd_user_ev_handler
    };

    UNUSED_VARIABLE(m_device_ctx);

    init_bsp();

    /* Initialize loging system and GPIOs. */
    log_init();

#if defined(APP_USBD_ENABLED) && APP_USBD_ENABLED
    ret = nrf_drv_clock_init();
    APP_ERROR_CHECK(ret);
    nrf_drv_clock_lfclk_request(NULL);
#endif

    ret = app_timer_init();
    APP_ERROR_CHECK(ret);

    app_usbd_serial_num_generate();

    ret = app_usbd_init(&usbd_config);
    APP_ERROR_CHECK(ret);
    NRF_LOG_INFO("USBD CDC ACM example started.");

    app_usbd_class_inst_t const * class_cdc_acm = app_usbd_cdc_acm_class_inst_get(&m_app_cdc_acm);
    ret = app_usbd_class_append(class_cdc_acm);
    APP_ERROR_CHECK(ret);

    /* Initialize the Zigbee CLI subsystem */
    zb_cli_init(ZIGBEE_CLI_ENDPOINT);

    ret = app_usbd_power_events_enable();
    APP_ERROR_CHECK(ret);

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
    zb_set_nvram_erase_at_start(ERASE_PERSISTENT_CONFIG);

    /* Register CLI Agent device context (endpoints). */
    ZB_AF_REGISTER_DEVICE_CTX(&cli_agent_ctx);

    /* Set the endpoint receive hook */
    ZB_AF_SET_ENDPOINT_HANDLER(ZIGBEE_CLI_ENDPOINT, cli_agent_ep_handler);

    /* Start ZigBee stack. */
    while(1)
    {
        if(m_send_flag)
        {
            static int  frame_counter;

            size_t size = sprintf(m_tx_buffer, "Hello USB CDC FA demo: %u\r\n", frame_counter);

            ret = app_usbd_cdc_acm_write(&m_app_cdc_acm, m_tx_buffer, size);
            if (ret == NRF_SUCCESS)
            {
                ++frame_counter;
            }
        }
        if (m_stack_started)
        {
            zboss_main_loop_iteration();
        }
        UNUSED_RETURN_VALUE(NRF_LOG_PROCESS());
        UNUSED_RETURN_VALUE(zb_cli_process());

        /* Sleep CPU only if there was no interrupt since last loop processing */
        __WFE();
    }
}


/**
 * @}
 */
