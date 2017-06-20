/*
 * File: rail-phy-configurations.h
 * Description: Include the proper RAIL configurations base on the selected 
 * radio board and phy options values.
 *
 * Author(s): 
 *   Maurizio Nanni, maurizio.nanni@silabs.com
 *   Viktor Huszar, viktor.huszar@silabs.com
 *
 * Copyright 2016 by Silicon Laboratories. All rights reserved.
 */
#include BOARD_HEADER

#if !defined(CONNECT_PHY_OPTION) && !defined(CONNECT_2_4_PHY) && !defined(EMBER_SCRIPTED_TEST)
#error "No RAIL phy option defined!"
#endif // !CONNECT_PHY_OPTION


/* 2.4 GHz + 915 MHz dual band EFR32FG/MG boards */
#if defined(BSP_WSTK_BRD4250A) || defined(BSP_WSTK_BRD4150B)
  #if defined(CONNECT_PHY_OPTION) && (CONNECT_PHY_OPTION == 0)
    /* US FCC 902 */
    #include "rail-phy-configurations/rail_config_902mhz_2gfsk_200kbps.h"
  #elif (CONNECT_PHY_OPTION == 1)
    /* Japan 915 */
    #include "rail-phy-configurations/rail_config_920mhz_2gfsk_100kbps.h"
  #elif (CONNECT_PHY_OPTION == 2)
    /* Brazil 902 */
    #include "rail-phy-configurations/rail_config_902mhz_2gfsk_200kbps.h"
  #elif (CONNECT_PHY_OPTION == 3)
    /* Korea 915 */
    #include "rail-phy-configurations/rail_config_917mhz_2gfsk_4_8kbps.h"
  #elif (CONNECT_PHY_OPTION == 4)
    /* DSSS 100 */
    #include "rail-phy-configurations/rail_config_915mhz_oqpsk_800kcps_100kbps.h"
  #elif (CONNECT_PHY_OPTION == 5)
    /* DSSS 250 */
    #include "rail-phy-configurations/rail_config_915mhz_oqpsk_2Mcps_250kbps.h"
  #elif (CONNECT_2_4_PHY == 100)
    #include "rail-phy-configurations/rail_config_2_4GHz_802154.h"
  #else 
    #error "RAIL phy option is not supported for the selected radio board!"
  #endif

/* 2.4 GHz + 868 MHz dual band EFR32FG/MG boards */
#elif defined(BSP_WSTK_BRD4250B) || defined(BSP_WSTK_BRD4150A)
  #if defined(CONNECT_PHY_OPTION) && (CONNECT_PHY_OPTION == 0)
    /* Europe 868 */
    #include "rail-phy-configurations/rail_config_863mhz_2gfsk_100kbps.h"
  #elif (CONNECT_2_4_PHY == 100)
    #include "rail-phy-configurations/rail_config_2_4GHz_802154.h"
  #else // CONNECT_PHY_OPTION
    #error "RAIL phy option is not supported for the selected radio board!"
  #endif

/* 2.4 GHz + 490 MHz dual band EFR32FG/MG boards */
#elif defined(BSP_WSTK_BRD4251A)
  #if defined(CONNECT_PHY_OPTION) && (CONNECT_PHY_OPTION == 0)
    /* China 490 */
    #include "rail-phy-configurations/rail_config_470mhz_2gfsk_10kbps.h"
  #elif (CONNECT_2_4_PHY == 100)
    #include "rail-phy-configurations/rail_config_2_4GHz_802154.h"
  #else // CONNECT_PHY_OPTION
    #error "RAIL phy option is not supported for the selected radio board!"
  #endif

/* 2.4 GHz + 434 MHz dual band EFR32FG/MG boards */
#elif defined(BSP_WSTK_BRD4152A) || defined(BSP_WSTK_BRD4251B)
  #if defined(CONNECT_PHY_OPTION) && (CONNECT_PHY_OPTION == 0)
    /* US FCC 434 */
    #include "rail-phy-configurations/rail_config_434mhz_2gfsk_200kbps.h"
  #elif (CONNECT_PHY_OPTION == 1)
    /* Korea 424 */
    #include "rail-phy-configurations/rail_config_424_7mhz_2gfsk_4_8kbps.h"
  #elif (CONNECT_PHY_OPTION == 2)
    /* Korea 447 */
    #include "rail-phy-configurations/rail_config_447_2mhz_2gfsk_4_8kbps.h"
  #elif (CONNECT_2_4_PHY == 100)
    #include "rail-phy-configurations/rail_config_2_4GHz_802154.h"
  #else // CONNECT_PHY_OPTION
    #error "RAIL phy option is not supported for the selected radio board!"
  #endif

/* 2.4 GHz + 169 MHz dual band EFR32FG/MG boards */
#elif defined(BSP_WSTK_BRD4251D)
  #if defined(CONNECT_PHY_OPTION) && (CONNECT_PHY_OPTION == 0)
    /* Europe 169 */
    #include "rail-phy-configurations/rail_config_169mhz_2gfsk_4_8kbps.h"
  #elif (CONNECT_2_4_PHY == 100)
    #include "rail-phy-configurations/rail_config_2_4GHz_802154.h"
  #else // CONNECT_PHY_OPTION
    #error "RAIL phy option is not supported for the selected radio board!"
  #endif

/* 2.4 GHz band EFR32FG/MG boards */
#elif defined(BSP_WSTK_BRD4252A)
  #if (CONNECT_2_4_PHY == 100)
    #include "rail-phy-configurations/rail_config_2_4GHz_802154.h"
  #else // CONNECT_PHY_OPTION
    #error "RAIL phy option is not supported for the selected radio board!"
  #endif

/* Hardware configurator */
/* FIXME: EMBERCONNECT-300: add support for other bands besides 2.4 */
#elif defined(EMBER_AF_USE_HWCONF)
  #include "rail-phy-configurations/rail_config_2_4GHz_802154.h"

#elif defined(EMBER_SCRIPTED_TEST)
  /* Nothing to define here */

#else
  #error "Radio board is not defined or not supported!"
#endif

