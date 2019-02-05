/**
 * debug_cli.c - debug command line interface
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.*
 */

#include <stdbool.h>

#include "nordic_common.h"
#include "nrf_cli.h"
#include "nrf_log.h"

typedef struct {
  const char *m_str;
  bool       *m_flag;
} DebugFlag_t;

#define DEBUG_FLAG(flag)  bool DEBUG_ ## flag = false;
#include "debug_flags.h"

#define DEBUG_FLAG(flag) {STRINGIFY(flag), &DEBUG_ ## flag},
static DebugFlag_t m_debug_flags[] = {
  #include "debug_flags.h"
};

static void debug_ctrl(nrf_cli_t const * p_cli, size_t argc, char **argv)
{
  bool flagValue = false;

  if (strcmp(argv[0], "enable") == 0) {
    flagValue = true;
  } else if (strcmp(argv[0], "disable") == 0) {
    flagValue = false;
  } else {
    nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Unknown option: %s\r\n", argv[0]);
    return;
  }

  if (argc <= 1) {
    nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "No debug flags specified\r\n");
    return;
  }

  for (int arg = 1; arg < argc; arg++) {
    const char *flagStr = argv[arg];
    bool flagFound = false;
    for (int i = 0; i < ARRAY_SIZE(m_debug_flags); i++) {
      if (strcmp(flagStr, m_debug_flags[i].m_str) == 0) {
        *(m_debug_flags[i].m_flag) = flagValue;
        flagFound = true;
        break;
      }
    }
    if (!flagFound) {
      nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "Unknown flag: %s\r\n", flagStr);
    }
  }
}

static void debug_list(nrf_cli_t const * p_cli, size_t argc, char **argv)
{
  for (int i = 0; i < ARRAY_SIZE(m_debug_flags); i++) {
    nrf_cli_fprintf(p_cli, NRF_CLI_NORMAL, "%s: %s\r\n",
                    m_debug_flags[i].m_str,
                    *(m_debug_flags[i].m_flag) ? "true" : "false");
  }
}

static void debug_flag_get(size_t idx, nrf_cli_static_entry_t * p_static);

NRF_CLI_CREATE_DYNAMIC_CMD(m_debug_flag, debug_flag_get);

static void debug_flag_get(size_t idx, nrf_cli_static_entry_t * p_static) {
    p_static->handler = NULL;
    p_static->p_help  = NULL;
    p_static->p_subcmd = &m_debug_flag;
    p_static->p_syntax = (idx < ARRAY_SIZE(m_debug_flags)) ?
                                m_debug_flags[idx].m_str : NULL;
}

NRF_CLI_CREATE_STATIC_SUBCMD_SET(m_sub_debug)
{
    NRF_CLI_CMD(disable, &m_debug_flag,
        "'debug disable <flag_0> .. <flag_n>' disables a specified debug flag",
        debug_ctrl),
    NRF_CLI_CMD(enable, &m_debug_flag,
        "'debug enable <flag_0> ...  <flag_n>' enables a specified debug flag",
        debug_ctrl),
    NRF_CLI_CMD(list, NULL, "debug flag list/status", debug_list),
    NRF_CLI_SUBCMD_SET_END
};

static void debug_cmd(const nrf_cli_t *p_cli, size_t argc, char **argv) {
  if ((argc == 1) || nrf_cli_help_requested(p_cli)) {
    nrf_cli_help_print(p_cli, NULL, 0);
    return;
  }

  nrf_cli_fprintf(p_cli, NRF_CLI_ERROR, "%s:%s%s\r\n", argv[0], " unknown parameter: ", argv[1]);
}

NRF_CLI_CMD_REGISTER(debug, &m_sub_debug, "Commands for controlling debug logging", debug_cmd);
